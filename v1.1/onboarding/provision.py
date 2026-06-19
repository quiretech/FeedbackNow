#!/usr/bin/env python3
"""
FlexBox unit provisioning: keys, build profile, flash, AWS IoT Core for LoRaWAN.

Examples (from repo root v1.1):
  python onboarding/provision.py list
  python onboarding/provision.py keys --preset lr-us915
  python onboarding/provision.py keys --preset seeed-eu868 --test
  python onboarding/provision.py all --preset lr-us915 --target qt-us915 --dry-run
  python onboarding/provision.py all --preset seeed-eu868 --target fbn-main --flash
  python onboarding/provision.py aws --target fbn-main --preset seeed-eu868 --unit UNIT-0042
  python onboarding/provision.py validate
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

_ONBOARDING = Path(__file__).resolve().parent
if str(_ONBOARDING) not in sys.path:
    sys.path.insert(0, str(_ONBOARDING))

from provision_lib import (
    ONBOARDING_DIR,
    REPO_ROOT,
    BuildProfile,
    apply_build_profile,
    generate_keys,
    registry_csv_path,
    stamp_provision_only,
)

TARGETS_JSON = ONBOARDING_DIR / "targets.json"
BATCH_REGISTER = ONBOARDING_DIR / "aws" / "batch_register_lorawan_devices.py"
COLLISION_CHECK = ONBOARDING_DIR / "aws" / "eui_collision_check.py"
APP_DIR = REPO_ROOT / "app"

PRESETS: dict[str, BuildProfile] = {
    "lr-us915": BuildProfile(hardware="lr62", region="us915"),
    "lr": BuildProfile(hardware="lr62", region="us915"),
    "seeed-us915": BuildProfile(hardware="seeed", region="us915"),
    "s-us915": BuildProfile(hardware="seeed", region="us915"),
    "s_us915": BuildProfile(hardware="seeed", region="us915"),
    "seeed-eu868": BuildProfile(hardware="seeed", region="eu868"),
    "s-eu868": BuildProfile(hardware="seeed", region="eu868"),
    "s_eu": BuildProfile(hardware="seeed", region="eu868"),
}


def _load_targets() -> dict:
    if not TARGETS_JSON.exists():
        raise RuntimeError(f"Missing {TARGETS_JSON}")
    with TARGETS_JSON.open(encoding="utf-8") as f:
        raw = json.load(f)
    out: dict[str, dict] = {}
    for tid, target in raw.items():
        out[tid] = target
        for alias in target.get("aliases", []):
            out[alias] = target
    return out


def _resolve_lorawan_region(
    *,
    profile: BuildProfile | None,
    region_flag: str | None,
) -> str:
    if profile is not None:
        return profile.region
    if region_flag:
        r = region_flag.strip().lower()
        if r in ("us915", "eu868"):
            return r
    raise ValueError(
        "LoRaWAN region required: pass --preset on `all`, or --preset / --region on `aws`"
    )


def _target_lorawan_block(target: dict, region: str) -> dict:
    """Device/service profile IDs for this AWS account + LoRaWAN region."""
    lorawan = target.get("lorawan")
    if isinstance(lorawan, dict) and region in lorawan:
        return lorawan[region]
    # Legacy flat targets (single region per target id)
    if target.get("lorawan_region") == region and target.get("device_profile_id"):
        return {
            "device_profile_id": target["device_profile_id"],
            "service_profile_id": target["service_profile_id"],
        }
    supported = sorted(lorawan.keys()) if isinstance(lorawan, dict) else [target.get("lorawan_region", "?")]
    raise ValueError(
        f"target has no LoRaWAN profile for region {region!r}; supported: {', '.join(supported)}"
    )


def _resolve_preset(name: str) -> BuildProfile:
    key = name.strip().lower().replace("_", "-")
    if key not in PRESETS:
        known = ", ".join(sorted(set(PRESETS.keys())))
        raise ValueError(f"Unknown preset {name!r}; known: {known}")
    return PRESETS[key]


def _cmd_list(_args: argparse.Namespace) -> int:
    with TARGETS_JSON.open(encoding="utf-8") as f:
        raw = json.load(f)
    print("=== Hardware + region presets (--preset) ===\n")
    seen: set[tuple[str, str]] = set()
    for name in sorted(PRESETS.keys()):
        p = PRESETS[name]
        sig = (p.hardware, p.region)
        if sig in seen:
            continue
        seen.add(sig)
        print(f"  {name:14}  hw={p.hardware:5}  region={p.region}")
    print("\n  Primary names: lr-us915 | seeed-us915 | seeed-eu868")
    print("\n=== AWS accounts (--target) ===")
    print("  Preset picks hardware + LoRaWAN region; --target picks AWS account only.\n")
    seen_ids: set[str] = set()
    for tid, t in sorted(raw.items()):
        if tid in seen_ids:
            continue
        seen_ids.add(tid)
        regions = ", ".join(sorted(t.get("lorawan", {}).keys()))
        aliases = t.get("aliases", [])
        alias_note = f"  (aliases: {', '.join(aliases)})" if aliases else ""
        print(
            f"  {tid:14}  profile={t.get('aws_profile','?'):16}  "
            f"regions=[{regions}]  {t.get('description','')}{alias_note}"
        )
    print("\n=== Typical one-step flow ===\n")
    print("  python onboarding/provision.py all --preset seeed-us915 --target quiretech")
    print("  python onboarding/provision.py all --preset seeed-eu868 --target fbn-main --flash")
    print("  python onboarding/provision.py all --preset lr-us915 --target fbn-eu --dry-run")
    print("  python onboarding/provision.py all --preset seeed-us915 --test   # demo_units CSV only")
    print("")
    return 0


def _cmd_validate(_args: argparse.Namespace) -> int:
    return subprocess.call([sys.executable, str(COLLISION_CHECK)], cwd=str(REPO_ROOT))


def _ensure_aws_profile(profile: str, *, dry_run: bool) -> None:
    if dry_run or not profile:
        return
    env = os.environ.copy()
    env["AWS_PROFILE"] = profile
    probe = subprocess.run(
        ["aws", "sts", "get-caller-identity"],
        env=env,
        capture_output=True,
        text=True,
    )
    if probe.returncode == 0:
        return
    print(f"AWS SSO login for profile {profile!r}...")
    subprocess.run(["aws", "sso", "login", "--profile", profile], check=True)


def _run_aws_register(
    *,
    target_id: str | None,
    profile: BuildProfile | None,
    region: str | None,
    test: bool,
    unit: str | None,
    last_only: bool,
    all_rows: bool,
    dry_run: bool,
    confirm: bool,
    csv_override: Path | None = None,
) -> int:
    if target_id is None and csv_override is None:
        print("ERROR: aws requires --target or explicit CSV via internal call", file=sys.stderr)
        return 2

    try:
        lorawan_region = _resolve_lorawan_region(profile=profile, region_flag=region)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    targets = _load_targets()
    target = targets.get(target_id) if target_id else None

    if csv_override is not None:
        csv_path = csv_override
        eu_flag = lorawan_region == "eu868"
        aws_profile = target.get("aws_profile", "") if target else os.environ.get("AWS_PROFILE", "")
        aws_region = target.get("aws_region", "us-east-1") if target else "us-east-1"
        if target:
            lw = _target_lorawan_block(target, lorawan_region)
            device_profile_id = lw["device_profile_id"]
            service_profile_id = lw["service_profile_id"]
            destination_name = target["destination_name"]
        else:
            device_profile_id = ""
            service_profile_id = ""
            destination_name = ""
    else:
        assert target_id is not None
        if target is None:
            print(f"ERROR: unknown target {target_id!r}", file=sys.stderr)
            return 2
        try:
            lw = _target_lorawan_block(target, lorawan_region)
        except ValueError as e:
            print(f"ERROR: {e}", file=sys.stderr)
            return 2
        eu_flag = lorawan_region == "eu868"
        csv_path = registry_csv_path(lorawan_region, test=test)
        aws_profile = target["aws_profile"]
        aws_region = target["aws_region"]
        device_profile_id = lw["device_profile_id"]
        service_profile_id = lw["service_profile_id"]
        destination_name = target["destination_name"]

    if not csv_path.exists():
        print(f"ERROR: registry CSV not found: {csv_path}", file=sys.stderr)
        return 2

    if unit:
        selection = "unit"
    elif all_rows:
        selection = "all-rows"
    elif last_only:
        selection = "last-only"
    else:
        selection = "all-rows-unfiltered"

    if selection == "all-rows" and not dry_run and not confirm:
        print(
            "ERROR: registering all CSV rows requires --confirm (or use --dry-run)",
            file=sys.stderr,
        )
        return 2

    if selection == "all-rows-unfiltered" and target_id and not unit:
        print(
            "ERROR: --target without --unit defaults to --last-only; pass --all-rows "
            "explicitly to register every row",
            file=sys.stderr,
        )
        return 2

    _ensure_aws_profile(aws_profile, dry_run=dry_run)

    cmd = [
        sys.executable,
        str(BATCH_REGISTER),
        str(csv_path),
        "--region",
        aws_region,
        "--device-profile-id",
        device_profile_id,
        "--service-profile-id",
        service_profile_id,
        "--destination-name",
        destination_name,
    ]
    if eu_flag:
        cmd.append("--EU")
    if dry_run:
        cmd.append("--dryrun")
    if unit:
        cmd.extend(["--unit", unit])
    elif last_only:
        cmd.append("--last-only")

    print(f"AWS target : {target_id or '(csv only)'}")
    print(f"LoRaWAN    : {lorawan_region}")
    print(f"CSV        : {csv_path}")
    print(f"Selection  : {selection}")
    print(f"Profile    : {aws_profile}")
    if dry_run:
        print("(dry run — no AWS API calls)")
    print("")

    env = os.environ.copy()
    if aws_profile:
        env["AWS_PROFILE"] = aws_profile
    return subprocess.call(cmd, cwd=str(REPO_ROOT), env=env)


def _cmd_keys(args: argparse.Namespace) -> int:
    profile = _resolve_preset(args.preset)
    code, _ = generate_keys(profile, test=args.test)
    return code


def _cmd_build(args: argparse.Namespace) -> int:
    profile = _resolve_preset(args.preset)
    try:
        apply_build_profile(profile)
    except (RuntimeError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    print(f"Build profile synced: {profile.hardware} + {profile.region}")
    return 0


def _cmd_flash(args: argparse.Namespace) -> int:
    runner = getattr(args, "runner", "jlink")
    cmd = ["west", "flash", "--runner", runner]
    print(f"Running in {APP_DIR}: {' '.join(cmd)}")
    return subprocess.call(cmd, cwd=str(APP_DIR))


def _cmd_aws(args: argparse.Namespace) -> int:
    if not args.target:
        print("ERROR: aws requires --target", file=sys.stderr)
        return 2

    profile = _resolve_preset(args.preset) if args.preset else None

    last_only = args.last_only
    if not args.unit and not args.all_rows:
        last_only = True

    return _run_aws_register(
        target_id=args.target,
        profile=profile,
        region=getattr(args, "region", None),
        test=args.test,
        unit=args.unit,
        last_only=last_only,
        all_rows=args.all_rows,
        dry_run=args.dry_run,
        confirm=args.confirm,
    )


def _cmd_all(args: argparse.Namespace) -> int:
    if not args.skip_keys:
        code = _cmd_keys(args)
        if code != 0:
            return code
        if not args.dry_run:
            code = _cmd_validate(args)
            if code != 0:
                print("ERROR: validate failed after keys; fix collisions before continuing", file=sys.stderr)
                return code

    if args.flash and not args.dry_run:
        code = _cmd_flash(args)
        if code != 0:
            return code

    if args.target:
        return _cmd_aws(args)

    return 0


def _add_common_flags(p: argparse.ArgumentParser) -> None:
    p.add_argument(
        "--preset",
        required=True,
        help="lr-us915 | seeed-us915 | seeed-eu868 (aliases: lr, s_us915, s_eu, ...)",
    )
    p.add_argument(
        "--test",
        action="store_true",
        help="Write registry under flexbox_euis/demo_units/ (not master CSVs)",
    )


def _add_aws_flags(p: argparse.ArgumentParser) -> None:
    p.add_argument(
        "--target",
        metavar="ID",
        help="AWS account from targets.json (quiretech, fbn-main, fbn-eu, fbn-admin; legacy aliases still work)",
    )
    p.add_argument(
        "--region",
        choices=("us915", "eu868"),
        help="LoRaWAN region for AWS step only (default: from --preset)",
    )
    p.add_argument(
        "--unit",
        metavar="ASSET_ID",
        help="Register a specific asset_id from the registry (overrides --last-only)",
    )
    p.add_argument(
        "--last-only",
        action="store_true",
        help="Register only the last CSV row (default when --target is set)",
    )
    p.add_argument(
        "--all-rows",
        action="store_true",
        help="Register every row in the CSV (requires --confirm unless --dry-run)",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        dest="dry_run",
        help="AWS: pass --dryrun to batch_register (no API calls)",
    )
    p.add_argument(
        "--confirm",
        action="store_true",
        help="Required with --all-rows for live AWS registration",
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="FlexBox unit provisioning")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("list", help="Show presets and AWS targets").set_defaults(
        func=_cmd_list
    )

    p_val = sub.add_parser("validate", help="Check master registries for EUI/key collisions")
    p_val.set_defaults(func=_cmd_validate)

    p_keys = sub.add_parser("keys", help="Generate EUIs, headers, registry row, build profile")
    _add_common_flags(p_keys)
    p_keys.set_defaults(func=_cmd_keys)

    p_build = sub.add_parser("build", help="Sync prj.conf + overlay only (no new keys)")
    _add_common_flags(p_build)
    p_build.set_defaults(func=_cmd_build)

    p_flash = sub.add_parser("flash", help="west flash in app/")
    p_flash.add_argument("--runner", default="jlink", help="west flash runner (default: jlink)")
    p_flash.set_defaults(func=_cmd_flash)

    p_aws = sub.add_parser("aws", help="Register device(s) in AWS IoT Core for LoRaWAN")
    p_aws.add_argument(
        "--preset",
        help="Hardware build preset; also selects LoRaWAN region + registry CSV unless --region is set",
    )
    p_aws.add_argument(
        "--test",
        action="store_true",
        help="Use flexbox_euis/demo_units/ registry CSV",
    )
    _add_aws_flags(p_aws)
    p_aws.set_defaults(func=_cmd_aws)

    p_all = sub.add_parser("all", help="keys → validate → [flash] → [aws]")
    _add_common_flags(p_all)
    _add_aws_flags(p_all)
    p_all.add_argument("--skip-keys", action="store_true", help="Skip key generation (re-flash/onboard only)")
    p_all.add_argument("--flash", action="store_true", help="Run west flash after keys")
    p_all.add_argument("--runner", default="jlink", help="west flash runner (default: jlink)")
    p_all.set_defaults(func=_cmd_all)

    p_stamp = sub.add_parser(
        "stamp",
        help="Update DEVICE_PROVISION_* UTC in onboarding_config.h only",
    )
    p_stamp.set_defaults(func=lambda _a: stamp_provision_only())

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
