# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT-0
#
# Repurposed to read onboarding/eui_registry.csv (asset_id, dev_eui, join_eui, app_key)
# and register all devices with AWS IoT Core for LoRaWAN.
#
# Usage:
#   From repo root (v1.1):
#     python onboarding/aws_onboarding/batchregistration/batch_register_lorawan_devices.py \
#       --region us-east-1 --device-profile-id <UUID> --service-profile-id <UUID> --destination-name <Name>
#   With dry run (no API calls):
#     ... --dryrun

import re
import argparse
import logging
import csv
from pathlib import Path

import boto3

# Default CSV: same directory as this script
SCRIPT_DIR = Path(__file__).resolve().parent

# Detect repo root (v1.1) and default to onboarding/eui_registry.csv
REPO_ROOT = SCRIPT_DIR.parents[2]  # batchregistration → aws_onboarding → onboarding → v1.1
DEFAULT_CSV = REPO_ROOT / "onboarding" / "eui_registry.csv"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger()


def _normalize_hex(s: str, length: int, name: str) -> str:
    h = re.sub(r"[^0-9A-Fa-f]", "", (s or "")).upper()
    if len(h) != length:
        raise ValueError(f"{name} must be {length} hex chars, got {len(h)}")
    return h


def load_eui_registry(csv_path: Path) -> list[dict]:
    """Load eui_registry.csv (asset_id, dev_eui, join_eui, app_key)."""
    rows = []
    with open(csv_path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        required = {"asset_id", "dev_eui", "join_eui", "app_key"}
        if reader.fieldnames and required <= set(reader.fieldnames):
            for i, r in enumerate(reader, start=2):
                try:
                    asset_id = (r.get("asset_id") or "").strip()
                    dev_eui = _normalize_hex(r.get("dev_eui") or "", 16, "dev_eui")
                    join_eui = _normalize_hex(r.get("join_eui") or "", 16, "join_eui")
                    app_key = _normalize_hex(r.get("app_key") or "", 32, "app_key")
                    rows.append({
                        "asset_id": asset_id or dev_eui,
                        "dev_eui": dev_eui,
                        "join_eui": join_eui,
                        "app_key": app_key,
                    })
                except Exception as e:
                    logger.error("Row %d: %s", i, e)
                    raise
    return rows


def validate_aws_resources(
    client,
    *,
    region: str,
    device_profile_id: str,
    service_profile_id: str,
    destination_name: str,
) -> None:
    """Verify device profile, service profile, and destination exist in this region. Raises on failure."""
    list_cmd = f"aws iotwireless list-device-profiles --region {region}"
    try:
        client.get_device_profile(Id=device_profile_id)
    except client.exceptions.ResourceNotFoundException:
        raise SystemExit(
            "Device profile not found in this region/account.\n"
            f"  Device Profile ID: {device_profile_id}\n"
            f"  Region: {region}\n"
            f"  List profiles: {list_cmd}"
        ) from None
    except Exception as e:
        logger.warning("Could not validate device profile (%s); continuing anyway: %s", device_profile_id, e)

    try:
        client.get_service_profile(Id=service_profile_id)
    except client.exceptions.ResourceNotFoundException:
        raise SystemExit(
            "Service profile not found in this region/account.\n"
            f"  Service Profile ID: {service_profile_id}\n"
            f"  Region: {region}\n"
            f"  List profiles: aws iotwireless list-service-profiles --region {region}"
        ) from None
    except Exception as e:
        logger.warning("Could not validate service profile (%s); continuing anyway: %s", service_profile_id, e)

    try:
        paginator = client.get_paginator("list_destinations")
        for page in paginator.paginate():
            for d in page.get("DestinationList", []):
                if d.get("Name") == destination_name:
                    return
        raise SystemExit(
            "Destination name not found in this region/account.\n"
            f"  Destination Name: {destination_name}\n"
            f"  Region: {region}\n"
            f"  List destinations: aws iotwireless list-destinations --region {region}"
        ) from None
    except SystemExit:
        raise
    except Exception as e:
        logger.warning("Could not validate destination (%s); continuing anyway: %s", destination_name, e)


def register_wireless_device(
    client,
    *,
    asset_id: str,
    dev_eui: str,
    join_eui: str,
    app_key: str,
    device_profile_id: str,
    service_profile_id: str,
    destination_name: str,
    dryrun: bool,
) -> bool:
    create_input = {
        "Type": "LoRaWAN",
        "Name": asset_id or dev_eui,
        "Description": asset_id or "",
        "DestinationName": destination_name,
        "LoRaWAN": {
            "DevEui": dev_eui,
            "DeviceProfileId": device_profile_id,
            "ServiceProfileId": service_profile_id,
            "OtaaV1_0_x": {
                "AppKey": app_key,
                "AppEui": join_eui,
            },
        },
    }
    logger.info("Creating device DevEui=%s Name=%s", dev_eui, asset_id or dev_eui)
    if dryrun:
        logger.info("[dryrun] would call create_wireless_device")
        return True
    try:
        client.create_wireless_device(**create_input)
        return True
    except Exception as e:
        logger.error("Error creating wireless device %s: %s", dev_eui, e)
        return False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Register all devices from eui_registry.csv with AWS IoT Core for LoRaWAN.",
    )
    parser.add_argument(
        "inputfilename",
        nargs="?",
        default=str(DEFAULT_CSV),
        help=f"Path to CSV (default: {DEFAULT_CSV})",
    )
    parser.add_argument("--region", "-r", required=True, help="AWS region (e.g. us-east-1)")
    parser.add_argument("--device-profile-id", required=True, help="AWS IoT Wireless Device Profile ID (UUID)")
    parser.add_argument("--service-profile-id", required=True, help="AWS IoT Wireless Service Profile ID (UUID)")
    parser.add_argument("--destination-name", required=True, help="AWS IoT Wireless Destination name (must exist)")
    parser.add_argument("--verbose", "-v", action="store_true", help="More output")
    parser.add_argument("--dryrun", "-d", action="store_true", help="Do not call AWS API; only log actions")
    parser.add_argument("--last-only", action="store_true", help="Only onboard the last entry in the CSV")


    args = parser.parse_args()
    csv_path = Path(args.inputfilename)

    if not csv_path.exists():
        logger.error("CSV not found: %s", csv_path)
        return 2

    rows = load_eui_registry(csv_path)
    if args.last_only:
        if rows:
            rows = [rows[-1]]
            logger.info("Processing only last CSV entry (asset_id=%s)", rows[0]["asset_id"])
        else:
            logger.warning("CSV is empty; nothing to process.")
            return 0


    if not rows:
        logger.warning("No rows in %s", csv_path)
        return 0

    logger.info("Loaded %d device(s) from %s", len(rows), csv_path)
    if args.dryrun:
        logger.info("Dry run: no API calls will be made")

    client = None if args.dryrun else boto3.client("iotwireless", region_name=args.region)

    if not args.dryrun and client:
        logger.info("Validating device profile, service profile, and destination in %s...", args.region)
        validate_aws_resources(
            client,
            region=args.region,
            device_profile_id=args.device_profile_id.strip(),
            service_profile_id=args.service_profile_id.strip(),
            destination_name=args.destination_name.strip(),
        )
        logger.info("Validation OK.")

    success = 0
    failed = 0
    for r in rows:
        if register_wireless_device(
            client,
            asset_id=r["asset_id"],
            dev_eui=r["dev_eui"],
            join_eui=r["join_eui"],
            app_key=r["app_key"],
            device_profile_id=args.device_profile_id.strip(),
            service_profile_id=args.service_profile_id.strip(),
            destination_name=args.destination_name.strip(),
            dryrun=args.dryrun,
        ):
            success += 1
        else:
            failed += 1

    logger.info("Done. Success=%d, failed=%d", success, failed)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
