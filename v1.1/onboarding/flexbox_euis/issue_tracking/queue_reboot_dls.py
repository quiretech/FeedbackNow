#!/usr/bin/env python3
"""Queue reboot downlinks (0x07 / Bw==) for v140 bug units after daily heartbeat.

Reads v140_bug_units_updated.csv, computes per-device send window from
expected_heartbeat_utc (FRD 4.9 jitter time, UTC), and queues reboot DLs via
AWS IoT Wireless when now >= heartbeat_utc + hours_after.

Rationale: if heartbeat fired (HB=1), MAPE-K keeps probing — queuing reboot
DLs a few hours after the scheduled heartbeat gives extra uplink/RX chances.

Usage:
  # Preview without AWS calls
  python3 queue_reboot_dls.py --dry-run

  # One shot (cron-friendly)
  python3 queue_reboot_dls.py --hours-after 2 --count 5

  # Keep terminal open; re-check every 30 minutes
  python3 queue_reboot_dls.py --loop --interval-minutes 30 --hours-after 2 --count 5

Already-queued rows (dl_queued_at set) are skipped unless --force is passed.
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
import time
from datetime import datetime, timedelta, time as dt_time, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_CSV = SCRIPT_DIR / "v140_bug_units_updated.csv"

REBOOT_PAYLOAD_B64 = "Bw=="  # DL_CMD_REBOOT 0x07
STATUS_COLUMNS = ("dl_send_at_utc", "dl_queued_at", "dl_count", "last_error")


def parse_heartbeat_utc(value: str) -> dt_time:
    value = (value or "").strip()
    if not value:
        raise ValueError("empty expected_heartbeat_utc")
    for fmt in ("%I:%M:%S %p", "%H:%M:%S"):
        try:
            return datetime.strptime(value, fmt).time()
        except ValueError:
            continue
    raise ValueError(f"unrecognized heartbeat time: {value!r}")


def compute_send_at(heartbeat: dt_time, hours_after: float, now_utc: datetime) -> datetime:
    """Earliest UTC time to queue reboot DLs for the current heartbeat cycle."""
    today_hb = datetime.combine(now_utc.date(), heartbeat, tzinfo=timezone.utc)
    yesterday_hb = today_hb - timedelta(days=1)
    yesterday_send_at = yesterday_hb + timedelta(hours=hours_after)
    today_send_at = today_hb + timedelta(hours=hours_after)

    if now_utc < today_hb:
        # Before today's heartbeat. Only use yesterday's window when it crosses
        # midnight (e.g. HB 22:00 UTC -> send at 00:00 next calendar day).
        if (
            now_utc >= yesterday_send_at
            and yesterday_send_at.date() == now_utc.date()
        ):
            return yesterday_send_at
        return today_send_at

    if now_utc < today_send_at:
        # Scheduled heartbeat passed; still in post-HB MAPE-K probing window.
        return today_send_at

    return today_send_at


def load_rows(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fieldnames = list(reader.fieldnames or [])
        rows = [dict(row) for row in reader]
    for col in STATUS_COLUMNS:
        if col not in fieldnames:
            fieldnames.append(col)
    return fieldnames, rows


def save_rows(path: Path, fieldnames: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def queue_reboot_dls(
    wireless_device_id: str,
    *,
    count: int,
    region: str,
    f_port: int,
    dry_run: bool,
) -> tuple[int, str]:
    queued = 0
    last_error = ""
    cmd_base = [
        "aws",
        "iotwireless",
        "send-data-to-wireless-device",
        "--id",
        wireless_device_id,
        "--transmit-mode",
        "1",
        "--payload-data",
        REBOOT_PAYLOAD_B64,
        "--wireless-metadata",
        f"LoRaWAN={{FPort={f_port}}}",
        "--region",
        region,
        "--output",
        "json",
    ]
    for _ in range(count):
        if dry_run:
            queued += 1
            continue
        try:
            subprocess.run(cmd_base, capture_output=True, text=True, check=True)
            queued += 1
        except subprocess.CalledProcessError as exc:
            last_error = (exc.stderr or exc.stdout or str(exc)).strip()
            break
    return queued, last_error


def run_once(args: argparse.Namespace) -> int:
    csv_path = Path(args.csv)
    if not csv_path.is_file():
        print(f"ERROR: CSV not found: {csv_path}", file=sys.stderr)
        return 1

    fieldnames, rows = load_rows(csv_path)
    now_utc = datetime.now(timezone.utc)
    ready = 0
    queued_devices = 0
    skipped = 0
    errors = 0

    for row in rows:
        device_id = (row.get("wirelessDeviceId") or "").strip()
        dev_eui = (row.get("dev_eui") or "").strip()
        heartbeat_raw = (row.get("expected_heartbeat_utc") or "").strip()

        if not device_id:
            skipped += 1
            continue

        if row.get("dl_queued_at") and not args.force:
            skipped += 1
            continue

        try:
            heartbeat = parse_heartbeat_utc(heartbeat_raw)
            send_at = compute_send_at(heartbeat, args.hours_after, now_utc)
        except ValueError as exc:
            row["last_error"] = str(exc)
            errors += 1
            print(f"SKIP {dev_eui or '?'}: {exc}")
            continue

        row["dl_send_at_utc"] = send_at.strftime("%Y-%m-%dT%H:%M:%SZ")

        if now_utc < send_at:
            print(
                f"WAIT {dev_eui} heartbeat={heartbeat_raw} "
                f"send_at={row['dl_send_at_utc']}"
            )
            continue

        ready += 1
        label = dev_eui or device_id
        print(
            f"QUEUE {label} id={device_id} count={args.count} "
            f"send_at={row['dl_send_at_utc']} dry_run={args.dry_run}"
        )

        count, err = queue_reboot_dls(
            device_id,
            count=args.count,
            region=args.region,
            f_port=args.f_port,
            dry_run=args.dry_run,
        )

        if count > 0:
            queued_devices += 1
            if not args.dry_run:
                row["dl_queued_at"] = now_utc.strftime("%Y-%m-%dT%H:%M:%SZ")
            else:
                row["dl_queued_at"] = f"DRY-RUN@{now_utc.strftime('%Y-%m-%dT%H:%M:%SZ')}"
        row["dl_count"] = str(count)
        row["last_error"] = err

        if err:
            errors += 1
            print(f"  ERROR after {count}/{args.count}: {err}")
        else:
            print(f"  OK queued {count}/{args.count}")

    if not args.dry_run:
        save_rows(csv_path, fieldnames, rows)
        print(f"\nUpdated {csv_path}")

    print(
        f"\nSummary: ready={ready} queued_devices={queued_devices} "
        f"skipped={skipped} errors={errors} now={now_utc.isoformat()}"
    )
    return 1 if errors else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Queue reboot downlinks after each device's UTC heartbeat window."
    )
    parser.add_argument(
        "--csv",
        default=str(DEFAULT_CSV),
        help=f"Input/output CSV (default: {DEFAULT_CSV.name})",
    )
    parser.add_argument(
        "--hours-after",
        type=float,
        default=2.0,
        help="Queue reboot DLs this many hours after scheduled heartbeat UTC (default: 2)",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=5,
        help="Number of reboot DLs to queue per device (default: 5)",
    )
    parser.add_argument("--region", default="us-east-1", help="AWS region")
    parser.add_argument("--f-port", type=int, default=1, help="LoRaWAN FPort metadata")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Compute windows and print actions without calling AWS",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Re-queue even if dl_queued_at is already set",
    )
    parser.add_argument(
        "--loop",
        action="store_true",
        help="Run continuously instead of one shot",
    )
    parser.add_argument(
        "--interval-minutes",
        type=int,
        default=30,
        help="Sleep interval between checks in --loop mode (default: 30)",
    )
    args = parser.parse_args(argv)

    if args.count < 1:
        print("ERROR: --count must be >= 1", file=sys.stderr)
        return 1

    if not args.loop:
        return run_once(args)

    print(
        f"Loop mode: every {args.interval_minutes} min, "
        f"hours_after={args.hours_after}, count={args.count}, "
        f"dry_run={args.dry_run}"
    )
    while True:
        try:
            run_once(args)
        except KeyboardInterrupt:
            print("\nStopped.")
            return 0
        print(f"\nSleeping {args.interval_minutes} minutes...\n")
        time.sleep(args.interval_minutes * 60)


if __name__ == "__main__":
    raise SystemExit(main())
