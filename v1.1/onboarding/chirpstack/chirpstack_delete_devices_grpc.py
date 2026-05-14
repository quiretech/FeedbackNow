#!/usr/bin/env python3
"""
Delete ALL devices in a ChirpStack application (gRPC).

Auth:
- Uses CHIRPSTACK_API_TOKEN env var or --token

⚠️ This permanently deletes devices.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any, Iterable

import grpc

ONBOARDING_DIR = Path(__file__).resolve().parent.parent
DEFAULT_API_KEY_FILE = ONBOARDING_DIR / "api_key"
DEFAULT_APPLICATION_ID = "1101c2be-d036-44bd-bfa5-2da441213bd0"


def _import_chirpstack_device_api():
    try:
        from chirpstack_api.api import device_pb2, device_pb2_grpc  # type: ignore
        return device_pb2, device_pb2_grpc
    except Exception:
        from chirpstack_api import api as _api  # type: ignore
        device_pb2 = getattr(_api, "device_pb2", _api)
        device_pb2_grpc = getattr(_api, "device_pb2_grpc", _api)
        return device_pb2, device_pb2_grpc


DEVICE_PB2, DEVICE_PB2_GRPC = _import_chirpstack_device_api()


def _grpc_call(
    stub: Any,
    method_names: Iterable[str],
    request: Any,
    *,
    metadata: list[tuple[str, str]],
):
    for m in method_names:
        fn = getattr(stub, m, None)
        if fn:
            return fn(request, metadata=metadata)
    raise AttributeError(f"None of these RPCs exist: {', '.join(method_names)}")


def list_all_devices(
    stub: Any,
    *,
    application_id: str,
    metadata: list[tuple[str, str]],
):
    devices = []
    limit = 100
    offset = 0

    while True:
        req = DEVICE_PB2.ListDevicesRequest(
            application_id=application_id,
            limit=limit,
            offset=offset,
        )

        resp = _grpc_call(
            stub,
            ["List"],
            req,
            metadata=metadata,
        )

        batch = list(resp.result)
        devices.extend(batch)

        if len(batch) < limit:
            break

        offset += limit

    return devices


def delete_device(
    stub: Any,
    *,
    dev_eui: str,
    metadata: list[tuple[str, str]],
):
    req = DEVICE_PB2.DeleteDeviceRequest(dev_eui=dev_eui)
    _grpc_call(
        stub,
        ["Delete"],
        req,
        metadata=metadata,
    )


def _load_token(api_key_file: Path, cli_token: str) -> str:
    if cli_token:
        return cli_token.strip()
    if api_key_file.exists():
        return api_key_file.read_text(encoding="utf-8").strip()
    import os
    return (os.environ.get("CHIRPSTACK_API_TOKEN") or "").strip()


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Delete all devices from a ChirpStack application (gRPC)")
    p.add_argument("--server", default="192.168.1.24:8080", help="ChirpStack gRPC host:port")
    p.add_argument("--application-id", default=DEFAULT_APPLICATION_ID, help="Application ID (UUID)")
    p.add_argument("--token", default="", help="API token (overrides onboarding/api_key)")
    p.add_argument("--api-key-file", default=str(DEFAULT_API_KEY_FILE), help="Path to file containing API token")
    p.add_argument("--dry-run", action="store_true", help="Show what would be deleted, but do nothing")

    args = p.parse_args(argv[1:])
    api_key_path = Path(args.api_key_file) if args.api_key_file else DEFAULT_API_KEY_FILE

    token = _load_token(api_key_path, args.token)
    if not token:
        print("ERROR: missing API token. Put your ChirpStack API key in onboarding/api_key or pass --token.", file=sys.stderr)
        return 2

    metadata = [("authorization", f"Bearer {token}")]

    channel = grpc.insecure_channel(args.server)
    stub = DEVICE_PB2_GRPC.DeviceServiceStub(channel)

    application_id = (args.application_id or DEFAULT_APPLICATION_ID).strip()
    print(f"Listing devices for application {application_id}...")
    devices = list_all_devices(
        stub,
        application_id=application_id,
        metadata=metadata,
    )

    if not devices:
        print("No devices found.")
        return 0

    print(f"Found {len(devices)} device(s).")

    ok = 0
    for d in devices:
        dev_eui = d.dev_eui
        name = getattr(d, "name", "")
        if args.dry_run:
            print(f"[dry-run] would delete {dev_eui} ({name})")
            ok += 1
            continue

        try:
            delete_device(
                stub,
                dev_eui=dev_eui,
                metadata=metadata,
            )
            print(f"[ok] deleted {dev_eui} ({name})")
            ok += 1
        except grpc.RpcError as e:
            print(f"[error] {dev_eui}: {e.code()} {e.details()}", file=sys.stderr)

    print(f"Done. Deleted {ok}/{len(devices)} device(s).")
    return 0 if ok == len(devices) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
