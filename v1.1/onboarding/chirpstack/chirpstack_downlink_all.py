#!/usr/bin/env python3
"""
Queue a downlink message to all devices in a ChirpStack application.

Reads API token from onboarding/api_key (or --api-key-file / --token).
Application ID defaults to InternalTesting (FBNOW_LNS_US915).

Usage:
  python onboarding/chirpstack/chirpstack_downlink_all.py --payload 0102
  python onboarding/chirpstack/chirpstack_downlink_all.py --payload "01 02 03" --f-port 11 --dry-run
"""

from __future__ import annotations

import argparse
import re
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


def _hex_to_bytes(hex_str: str) -> bytes:
    hex_clean = re.sub(r"[^0-9A-Fa-f]", "", (hex_str or ""))
    if len(hex_clean) % 2:
        raise ValueError("Payload hex must have an even number of digits")
    return bytes.fromhex(hex_clean)


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
        resp = _grpc_call(stub, ["List"], req, metadata=metadata)
        batch = list(resp.result)
        devices.extend(batch)
        if len(batch) < limit:
            break
        offset += limit
    return devices


def enqueue_downlink(
    stub: Any,
    *,
    dev_eui: str,
    f_port: int,
    data: bytes,
    confirmed: bool,
    metadata: list[tuple[str, str]],
) -> Any:
    # ChirpStack expects dev_eui as 8-byte hex string (no separators)
    dev_eui_hex = re.sub(r"[^0-9A-Fa-f]", "", dev_eui)
    if len(dev_eui_hex) != 16:
        raise ValueError(f"dev_eui must be 16 hex chars, got {len(dev_eui_hex)}")
    req = DEVICE_PB2.EnqueueDeviceQueueItemRequest()
    req.queue_item.dev_eui = dev_eui_hex
    req.queue_item.f_port = f_port
    req.queue_item.confirmed = confirmed
    req.queue_item.data = data
    return _grpc_call(stub, ["Enqueue", "EnqueueDeviceQueueItem"], req, metadata=metadata)


def _load_token(api_key_file: Path, cli_token: str) -> str:
    if cli_token:
        return cli_token.strip()
    if api_key_file.exists():
        return api_key_file.read_text(encoding="utf-8").strip()
    import os
    return (os.environ.get("CHIRPSTACK_API_TOKEN") or "").strip()


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(
        description="Queue a downlink to all devices in the ChirpStack application (InternalTesting)."
    )
    p.add_argument("--server", default="192.168.1.24:8080", help="ChirpStack gRPC host:port")
    p.add_argument("--application-id", default=DEFAULT_APPLICATION_ID, help="Application ID (UUID)")
    p.add_argument("--payload", required=True, help="Downlink payload as hex (e.g. 0102 or '01 02 03')")
    p.add_argument("--f-port", type=int, default=10, help="LoRaWAN FPort (default 10)")
    p.add_argument("--confirmed", action="store_true", help="Request confirmed downlink")
    p.add_argument("--token", default="", help="API token (overrides onboarding/api_key)")
    p.add_argument("--api-key-file", default=str(DEFAULT_API_KEY_FILE), help="Path to file containing API token")
    p.add_argument("--dry-run", action="store_true", help="List devices and payload only; do not enqueue")

    args = p.parse_args(argv[1:])
    api_key_path = Path(args.api_key_file) if args.api_key_file else DEFAULT_API_KEY_FILE

    token = _load_token(api_key_path, args.token)
    if not token:
        print(
            "ERROR: missing API token. Put your ChirpStack API key in onboarding/api_key or pass --token.",
            file=sys.stderr,
        )
        return 2

    try:
        data = _hex_to_bytes(args.payload)
    except ValueError as e:
        print(f"ERROR: invalid --payload: {e}", file=sys.stderr)
        return 2

    application_id = (args.application_id or DEFAULT_APPLICATION_ID).strip()
    metadata = [("authorization", f"Bearer {token}")]

    channel = grpc.insecure_channel(args.server)
    stub = DEVICE_PB2_GRPC.DeviceServiceStub(channel)

    print(f"Listing devices for application {application_id}...")
    devices = list_all_devices(stub, application_id=application_id, metadata=metadata)
    if not devices:
        print("No devices found.")
        return 0

    print(f"Found {len(devices)} device(s). Payload: {data.hex()} ({len(data)} bytes), f_port={args.f_port}")
    if args.dry_run:
        for d in devices:
            print(f"  [dry-run] would enqueue to {d.dev_eui} ({getattr(d, 'name', '')})")
        return 0

    ok = 0
    for d in devices:
        dev_eui = d.dev_eui
        name = getattr(d, "name", "")
        try:
            enqueue_downlink(
                stub,
                dev_eui=dev_eui,
                f_port=args.f_port,
                data=data,
                confirmed=args.confirmed,
                metadata=metadata,
            )
            print(f"[ok] enqueued to {dev_eui} ({name})")
            ok += 1
        except Exception as e:
            print(f"[error] {dev_eui} ({name}): {e}", file=sys.stderr)

    print(f"Done. Enqueued to {ok}/{len(devices)} device(s).")
    return 0 if ok == len(devices) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
