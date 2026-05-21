# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT-0
#
# Repurposed to read onboarding/flexbox_euis/eui_registry.csv (asset_id, dev_eui, join_eui, app_key,
# optional name_prefix / tag_client / decal_type / hw_profile for AWS naming and tags) and register all devices
# with AWS IoT Core for LoRaWAN.
#
# Usage:
#   From repo root (v1.1):
#     python onboarding/aws/batch_register_lorawan_devices.py \
#       --region us-east-1 --device-profile-id <UUID> --service-profile-id <UUID> --destination-name <Name>
#   EU868 registry (eui_registry_EU868.csv):
#     ... --EU
#   With dry run (no API calls):
#     ... --dryrun

import re
import argparse
import logging
import csv
from pathlib import Path

import boto3

ONBOARDING_DIR = Path(__file__).resolve().parent.parent
_REGISTRY_DIR = ONBOARDING_DIR / "flexbox_euis"
DEFAULT_CSV = _REGISTRY_DIR / "eui_registry.csv"
EU868_CSV = _REGISTRY_DIR / "eui_registry_EU868.csv"

# AWS IoT Wireless resource tag: stable key; CSV column `hw_profile` supplies value.
_FLEXBOX_HW_PROFILE_TAG_KEY = "Variant"
# Values written by gen_euis.py from DEVICE_HW_VARIANT in onboarding_config.h.
_VALID_HW_PROFILES = frozenset({"FLEXBOX_PLUS", "FLEXBOX"})
# Older gen_euis rows before product rename
_LEGACY_HW_PROFILE_ALIASES = {"EPD_NFC": "FLEXBOX_PLUS", "NFC_BUTTONS": "FLEXBOX"}
_AWS_WIRELESS_NAME_MAX = 256
_AWS_TAG_KEY_MAX = 128
_AWS_TAG_VALUE_MAX = 256
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


def _truncate(s: str, max_len: int) -> str:
    if len(s) <= max_len:
        return s
    return s[:max_len]


def wireless_device_name(
    asset_id: str, dev_eui: str, name_prefix: str, tag_client: str
) -> str:
    """
    AWS wireless device Name (unit = asset_id):
    - No name_prefix and no tag_client: plain unit only (asset_id, or dev_eui fallback).
    - Otherwise: ``<left>-<unit>`` — left is name_prefix when non-empty, else tag_client;
      one hyphen before the unit (trailing hyphens on left trimmed).
    """
    base = (asset_id or "").strip() or dev_eui
    p = (name_prefix or "").strip()
    client = (tag_client or "").strip()
    left = p if p else client
    if not left:
        return _truncate(base, _AWS_WIRELESS_NAME_MAX)
    left = left.rstrip("-").strip() or (p or client)
    out = f"{left}-{base}"
    return _truncate(out, _AWS_WIRELESS_NAME_MAX)


def wireless_device_tags(row: dict) -> list[dict[str, str]]:
    """
    AWS CreateWirelessDevice Tags (list of {Key, Value} dicts).

    Always includes Variant from CSV `hw_profile` (gen_euis: FLEXBOX_PLUS | FLEXBOX).
    Optionally Client / Decal from tag_client / decal_type when non-blank.
    """
    tags: list[dict[str, str]] = []
    prof = (row.get("hw_profile") or "").strip()
    if prof in _LEGACY_HW_PROFILE_ALIASES:
        prof = _LEGACY_HW_PROFILE_ALIASES[prof]
    if not prof:
        prof = "UNKNOWN"
        logger.warning(
            "asset_id=%s: missing hw_profile in CSV; using tag %s=%s "
            "(re-run gen_euis to normalize CSV or set hw_profile to FLEXBOX_PLUS or FLEXBOX)",
            row.get("asset_id"),
            _FLEXBOX_HW_PROFILE_TAG_KEY,
            prof,
        )
    elif prof not in _VALID_HW_PROFILES:
        logger.warning(
            "asset_id=%s: hw_profile=%r is not in %s (sending as-is)",
            row.get("asset_id"),
            prof,
            sorted(_VALID_HW_PROFILES),
        )
    tags.append(
        {
            "Key": _truncate(_FLEXBOX_HW_PROFILE_TAG_KEY, _AWS_TAG_KEY_MAX),
            "Value": _truncate(prof, _AWS_TAG_VALUE_MAX),
        }
    )
    client = (row.get("tag_client") or "").strip()
    if client:
        tags.append(
            {
                "Key": _truncate("Client", _AWS_TAG_KEY_MAX),
                "Value": _truncate(client, _AWS_TAG_VALUE_MAX),
            }
        )
    decal = (row.get("decal_type") or row.get("tag_location") or "").strip()
    if decal:
        tags.append(
            {
                "Key": _truncate("Decal", _AWS_TAG_KEY_MAX),
                "Value": _truncate(decal, _AWS_TAG_VALUE_MAX),
            }
        )
    return tags


def load_eui_registry(csv_path: Path) -> list[dict]:
    """Load eui_registry CSV. Required columns: asset_id, dev_eui, join_eui, app_key (extras preserved)."""
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
                        "name_prefix": (r.get("name_prefix") or "").strip(),
                        "tag_client": (r.get("tag_client") or "").strip(),
                        "decal_type": (
                            (r.get("decal_type") or r.get("tag_location") or "").strip()
                        ),
                        "hw_profile": (r.get("hw_profile") or "").strip(),
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
    name_prefix: str,
    tag_client: str,
    decal_type: str,
    hw_profile: str,
    device_profile_id: str,
    service_profile_id: str,
    destination_name: str,
    dryrun: bool,
) -> bool:
    row = {
        "asset_id": asset_id,
        "tag_client": tag_client,
        "decal_type": decal_type,
        "hw_profile": hw_profile,
    }
    display_name = wireless_device_name(asset_id, dev_eui, name_prefix, tag_client)
    tags = wireless_device_tags(row)
    create_input = {
        "Type": "LoRaWAN",
        "Name": display_name,
        "Description": _truncate(display_name, 512),
        "DestinationName": destination_name,
        "Tags": tags,
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
    logger.info(
        "Creating device DevEui=%s Name=%s Tags=%s",
        dev_eui,
        display_name,
        tags,
    )
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
        description=(
            "Register all devices from eui_registry.csv with AWS IoT Core for LoRaWAN. "
            "Optional CSV: tag_client / decal_type (AWS tags Client / Decal; client also controls device Name). "
            "Name = asset_id if name_prefix and tag_client are both empty; else (name_prefix or tag_client)-asset_id "
            "(hyphen before unit; name_prefix wins when both set). "
            "hw_profile (FLEXBOX_PLUS | FLEXBOX from gen_euis — always tag Variant)."
        ),
    )
    parser.add_argument(
        "inputfilename",
        nargs="?",
        default=None,
        help="Path to CSV (default: eui_registry_EU868.csv if --EU, else eui_registry.csv)",
    )
    parser.add_argument(
        "--EU",
        action="store_true",
        help="Use EU868 registry (eui_registry_EU868.csv) when no input file is given",
    )
    parser.add_argument("--region", "-r", required=True, help="AWS region (e.g. us-east-1)")
    parser.add_argument("--device-profile-id", required=True, help="AWS IoT Wireless Device Profile ID (UUID)")
    parser.add_argument("--service-profile-id", required=True, help="AWS IoT Wireless Service Profile ID (UUID)")
    parser.add_argument("--destination-name", required=True, help="AWS IoT Wireless Destination name (must exist)")
    parser.add_argument("--verbose", "-v", action="store_true", help="More output")
    parser.add_argument("--dryrun", "-d", action="store_true", help="Do not call AWS API; only log actions")
    parser.add_argument("--last-only", action="store_true", help="Only onboard the last entry in the CSV")


    args = parser.parse_args()
    if args.inputfilename is not None:
        csv_path = Path(args.inputfilename)
    else:
        csv_path = EU868_CSV if args.EU else DEFAULT_CSV

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
            name_prefix=r.get("name_prefix") or "",
            tag_client=r.get("tag_client") or "",
            decal_type=r.get("decal_type") or "",
            hw_profile=r.get("hw_profile") or "",
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
