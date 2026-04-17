#!/usr/bin/env python3
"""
Onboard (create / update) devices in ChirpStack from eui_registry.csv.

This script is designed to be re-runnable (idempotent-ish):
- If a device exists, it updates it.
- If device keys exist, it updates them.

Auth:
- Pass an API token via the CHIRPSTACK_API_TOKEN environment variable (recommended),
  or via --token.

Docs:
- ChirpStack API reference: https://www.chirpstack.io/docs/chirpstack/api/api.html
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable

import grpc

# Script lives in onboarding/; API key and CSV live there.
ONBOARDING_DIR = Path(__file__).resolve().parent
DEFAULT_API_KEY_FILE = ONBOARDING_DIR / "api_key"
DEFAULT_CSV_PATH = ONBOARDING_DIR / "eui_registry.csv"
DEFAULT_APPLICATION_ID = "1101c2be-d036-44bd-bfa5-2da441213bd0"
DEFAULT_DEVICE_PROFILE_ID = "b89bb1a9-5ae7-4bd0-9b35-e88bb376eee7"


def _import_chirpstack_device_api():
    """
    chirpstack-api packaging differs slightly across versions.
    Prefer the canonical chirpstack_api.api.* modules, but fall back gracefully.
    """
    try:
        from chirpstack_api.api import device_pb2, device_pb2_grpc  # type: ignore

        return device_pb2, device_pb2_grpc
    except Exception as e:
        # Common on Windows when protobuf is too old for the installed chirpstack-api:
        # ImportError: cannot import name 'runtime_version' from 'google.protobuf'
        msg = str(e)
        if "google.protobuf" in msg and "runtime_version" in msg:
            raise ImportError(
                "Your installed 'protobuf' package is too old for the installed 'chirpstack-api'.\n"
                "Fix:\n"
                "  python -m pip install -U protobuf\n"
                "or reinstall from requirements:\n"
                "  python -m pip install -U -r requirements.txt\n"
            ) from e

        # Fallbacks used by some examples / older layouts
        from chirpstack_api import api as _api  # type: ignore

        device_pb2 = getattr(_api, "device_pb2", None) or getattr(_api, "device", None) or _api
        device_pb2_grpc = getattr(_api, "device_pb2_grpc", None) or getattr(_api, "device_grpc", None) or _api
        return device_pb2, device_pb2_grpc


DEVICE_PB2, DEVICE_PB2_GRPC = _import_chirpstack_device_api()


def _clean_hex(s: str) -> str:
    return re.sub(r"[^0-9A-Fa-f]", "", (s or "")).upper()


def _require_hex_len(name: str, s: str, n: int) -> str:
    s = _clean_hex(s)
    if len(s) != n:
        raise ValueError(f"{name} must be {n} hex chars, got {len(s)} ({s!r})")
    return s


def _grpc_call(
    stub: Any,
    method_names: Iterable[str],
    request: Any,
    *,
    metadata: list[tuple[str, str]],
):
    last_err: Exception | None = None
    for m in method_names:
        fn = getattr(stub, m, None)
        if fn is None:
            continue
        return fn(request, metadata=metadata)
    if last_err:
        raise last_err
    raise AttributeError(f"None of these RPCs exist on stub: {', '.join(method_names)}")


def _is_already_exists(err: grpc.RpcError) -> bool:
    try:
        return err.code() == grpc.StatusCode.ALREADY_EXISTS
    except Exception:
        return False


def _is_not_found(err: grpc.RpcError) -> bool:
    try:
        return err.code() == grpc.StatusCode.NOT_FOUND
    except Exception:
        return False


@dataclass(frozen=True)
class RegistryRow:
    asset_id: str
    dev_eui: str
    join_eui: str
    app_key: str
    # Optional: older registries included this column.
    timestamp_utc: str = ""


def _read_registry(path: str) -> list[RegistryRow]:
    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        # New format omits timestamp_utc, but we keep it backward compatible.
        required = {"asset_id", "dev_eui", "join_eui", "app_key"}
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise ValueError(f"CSV missing columns: {sorted(missing)}")

        out: list[RegistryRow] = []
        for i, r in enumerate(reader, start=2):
            try:
                out.append(
                    RegistryRow(
                        timestamp_utc=(r.get("timestamp_utc") or "").strip(),
                        asset_id=(r.get("asset_id") or "").strip(),
                        dev_eui=_require_hex_len("dev_eui", r.get("dev_eui") or "", 16),
                        join_eui=_require_hex_len("join_eui", r.get("join_eui") or "", 16),
                        app_key=_require_hex_len("app_key", r.get("app_key") or "", 32),
                    )
                )
            except Exception as e:
                raise ValueError(f"Invalid row at line {i}: {e}") from e
        return out


def _set_if_present(msg: Any, field: str, value: Any) -> None:
    # Protobuf messages raise AttributeError if field doesn't exist.
    try:
        getattr(msg, field)
    except Exception:
        return
    try:
        setattr(msg, field, value)
    except Exception:
        return


def _put_map_if_present(msg: Any, field: str, key: str, value: str) -> None:
    try:
        m = getattr(msg, field)
    except Exception:
        return
    try:
        m[key] = value
    except Exception:
        return


def upsert_device_and_keys(
    stub: Any,
    *,
    application_id: str,
    device_profile_id: str,
    row: RegistryRow,
    skip_fcnt_check: bool,
    dry_run: bool,
    metadata: list[tuple[str, str]],
) -> None:
    name = row.asset_id or row.dev_eui

    device = DEVICE_PB2.Device(
        dev_eui=row.dev_eui,
        name=name,
        description=row.asset_id or "",
        application_id=application_id,
        device_profile_id=device_profile_id,
    )
    _set_if_present(device, "join_eui", row.join_eui)
    _set_if_present(device, "skip_fcnt_check", bool(skip_fcnt_check))
    _put_map_if_present(device, "tags", "asset_id", row.asset_id)
    _put_map_if_present(device, "tags", "join_eui", row.join_eui)

    if dry_run:
        print(f"[dry-run] would upsert device {row.dev_eui} name={name!r}")
    else:
        try:
            _grpc_call(stub, ["Create"], DEVICE_PB2.CreateDeviceRequest(device=device), metadata=metadata)
            print(f"[ok] created device {row.dev_eui} ({name})")
        except grpc.RpcError as e:
            if _is_already_exists(e):
                _grpc_call(stub, ["Update"], DEVICE_PB2.UpdateDeviceRequest(device=device), metadata=metadata)
                print(f"[ok] updated device {row.dev_eui} ({name})")
            else:
                raise

    keys = DEVICE_PB2.DeviceKeys(dev_eui=row.dev_eui)
    # ChirpStack v4 supports LoRaWAN 1.0.x and 1.1; fields depend on version.
    _set_if_present(keys, "nwk_key", row.app_key)
    _set_if_present(keys, "app_key", row.app_key)
    # Sometimes the field is called 'key' in older layouts (rare); try it too.
    _set_if_present(keys, "key", row.app_key)

    if dry_run:
        print(f"[dry-run] would upsert keys for {row.dev_eui}")
        return

    create_req = DEVICE_PB2.CreateDeviceKeysRequest(device_keys=keys)
    update_req = DEVICE_PB2.UpdateDeviceKeysRequest(device_keys=keys)

    try:
        _grpc_call(stub, ["CreateKeys", "CreateDeviceKeys"], create_req, metadata=metadata)
        print(f"[ok] created keys for {row.dev_eui}")
    except grpc.RpcError as e:
        if _is_already_exists(e):
            _grpc_call(stub, ["UpdateKeys", "UpdateDeviceKeys"], update_req, metadata=metadata)
            print(f"[ok] updated keys for {row.dev_eui}")
        else:
            raise


def _load_token(api_key_file: Path, cli_token: str) -> str:
    """API token: from --token, else from onboarding/api_key file, else env."""
    if cli_token:
        return cli_token.strip()
    if api_key_file.exists():
        return api_key_file.read_text(encoding="utf-8").strip()
    import os
    return (os.environ.get("CHIRPSTACK_API_TOKEN") or "").strip()


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Onboard devices from eui_registry.csv into ChirpStack (gRPC).")
    p.add_argument("--server", default="192.168.0.184:8080", help="ChirpStack gRPC host:port (default: %(default)s)")
    p.add_argument("--csv", dest="csv_path", default=str(DEFAULT_CSV_PATH), help="Path to eui_registry.csv")
    p.add_argument("--application-id", default=DEFAULT_APPLICATION_ID, help="ChirpStack Application ID (UUID)")
    p.add_argument("--device-profile-id", default=DEFAULT_DEVICE_PROFILE_ID, help="ChirpStack Device Profile ID (UUID)")
    p.add_argument("--token", default="", help="API token (overrides onboarding/api_key)")
    p.add_argument("--api-key-file", default=str(DEFAULT_API_KEY_FILE), help="Path to file containing API token")
    p.add_argument("--skip-fcnt-check", action="store_true", help="Set skip_fcnt_check=true on devices (if supported)")
    p.add_argument("--dry-run", action="store_true", help="Parse CSV and print actions, but do not call ChirpStack")
    p.add_argument("--limit", type=int, default=0, help="Only process first N rows (0 = all)")

    args = p.parse_args(argv[1:])
    csv_path = Path(args.csv_path) if args.csv_path else DEFAULT_CSV_PATH
    api_key_path = Path(args.api_key_file) if args.api_key_file else DEFAULT_API_KEY_FILE

    token = _load_token(api_key_path, args.token)
    if not token:
        print(
            "ERROR: missing API token. Put your ChirpStack API key in onboarding/api_key or pass --token.",
            file=sys.stderr,
        )
        return 2

    rows = _read_registry(str(csv_path))
    if args.limit and args.limit > 0:
        rows = rows[: args.limit]

    metadata = [("authorization", f"Bearer {token}")]

    channel = grpc.insecure_channel(args.server)
    stub = DEVICE_PB2_GRPC.DeviceServiceStub(channel)

    ok = 0
    for row in rows:
        try:
            upsert_device_and_keys(
                stub,
                application_id=args.application_id.strip(),
                device_profile_id=args.device_profile_id.strip(),
                row=row,
                skip_fcnt_check=args.skip_fcnt_check,
                dry_run=args.dry_run,
                metadata=metadata,
            )
            ok += 1
        except grpc.RpcError as e:
            print(f"[error] {row.dev_eui} ({row.asset_id}): {e.code()} {e.details()}", file=sys.stderr)
        except Exception as e:
            print(f"[error] {row.dev_eui} ({row.asset_id}): {e}", file=sys.stderr)

    print(f"Done. Processed {len(rows)} device(s); success={ok} failed={len(rows) - ok}")
    return 0 if ok == len(rows) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))


