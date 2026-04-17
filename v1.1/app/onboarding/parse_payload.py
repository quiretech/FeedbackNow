#!/usr/bin/env python3
"""Parse 11-byte LoRa payloads used by firmware payload_gen.

Usage:
  python3 parse_payload.py <PAYLOAD>
  python3 parse_payload.py --format {auto,hex,b64} <PAYLOAD>

Examples:
  python3 parse_payload.py 6945A731000400000A0000
  python3 parse_payload.py "aUWnMQAEAAAKAAA="
  python3 parse_payload.py --format b64 "<aUWnMQAEAAAKAAA=>"

Format (11 bytes):
  [0..3] timestamp (epoch seconds, big-endian)
  [4]    event type
  [5..]  event-specific

Event types (from app/include/payload_gen.h):
  0x00 = EVT_BUTTON:         [5]=button_id, [6..8]=counter (24-bit BE), [9..10]=reserved
  0x01 = EVT_NFC_IN:         [5..8]=card_data_4, [9..10]=reserved
  0x02 = EVT_NFC_OUT:        [5..8]=card_data_4, [9..10]=reserved
  0x03 = EVT_NFC_VOTE:       [5]=button_id, [6..9]=card_data_4, [10]=reserved
  0x10 = EVT_BATTERY_STATUS: [5..6]=battery_mv (16-bit BE), [7]=percent, [8]=flags, [9..10]=reserved
  0x12 = EVT_COUNTER_SYNC:   [5]=button_id, [6..8]=counter (24-bit BE), [9..10]=reserved
  0x13 = EVT_DEVICE_STATE_SNAPSHOT (FPort 20 HK: post-join, daily HK, DL 0x04): [0..3]=ts BE,
         [4]=0x13, [5..8]=last_cleaned_epoch BE (EEPROM), [9..10]=tz_offset_min int16 BE
  0x14 = EVT_DEVICE_VERSION_INFO (FPort 21, DL 0x08 fw/hw version query): [0..3]=ts BE, [4]=0x14,
         [5..7]=fw major/minor/patch, [8..10]=hw major/minor/patch
"""

from __future__ import annotations

import datetime
import base64
import sys


EVT_NAMES = {
    0x00: "EVT_BUTTON",
    0x01: "EVT_NFC_IN",
    0x02: "EVT_NFC_OUT",
    0x03: "EVT_NFC_VOTE",
    0x10: "EVT_BATTERY_STATUS",
    0x11: "EVT_LOW_BATTERY",
    0x12: "EVT_COUNTER_SYNC",
    0x13: "EVT_DEVICE_STATE_SNAPSHOT",
    0x14: "EVT_DEVICE_VERSION_INFO",
    0xFF: "EVT_FUTURE",
}


def _clean_wrapped(s: str) -> str:
    s = s.strip()
    if s.startswith("<") and s.endswith(">"):
        s = s[1:-1].strip()
    return s


def _clean_hex(s: str) -> str:
    s = _clean_wrapped(s)
    if s.startswith("0x") or s.startswith("0X"):
        s = s[2:]
    s = "".join(s.split())
    return s


def _clean_b64(s: str) -> str:
    s = _clean_wrapped(s)
    s = "".join(s.split())
    return s


def _fmt_hex(b: bytes) -> str:
    return "".join(f"{x:02X}" for x in b)


def _parse_hex(s: str) -> bytes:
    return bytes.fromhex(_clean_hex(s))


def _parse_b64(s: str) -> bytes:
    raw = _clean_b64(s)
    # Pad to a multiple of 4 so users can pass unpadded base64.
    pad_len = (-len(raw)) % 4
    raw_padded = raw + ("=" * pad_len)
    return base64.b64decode(raw_padded, validate=True)


def _decode_input(s: str, fmt: str) -> tuple[bytes, str]:
    if fmt == "hex":
        return _parse_hex(s), "hex"
    if fmt == "b64":
        return _parse_b64(s), "b64"
    if fmt != "auto":
        raise ValueError(f"Unsupported format: {fmt}")

    # Auto mode: hex first (backward compatible), then base64.
    try:
        return _parse_hex(s), "hex"
    except ValueError:
        return _parse_b64(s), "b64"


def main(argv: list[str]) -> int:
    if len(argv) < 2 or any(arg in {"-h", "--help"} for arg in argv[1:]):
        print(__doc__.rstrip())
        return 2

    fmt = "auto"
    payload_arg = ""
    i = 1
    while i < len(argv):
        arg = argv[i]
        if arg == "--format":
            if i + 1 >= len(argv):
                print("Missing value for --format (expected auto|hex|b64)")
                return 2
            fmt = argv[i + 1].strip().lower()
            i += 2
            continue
        if arg.startswith("--format="):
            fmt = arg.split("=", 1)[1].strip().lower()
            i += 1
            continue
        if arg.startswith("-"):
            print(f"Unknown option: {arg}")
            return 2
        if payload_arg:
            print("Unexpected extra argument")
            return 2
        payload_arg = arg
        i += 1

    if not payload_arg:
        print("Missing payload argument")
        return 2
    if fmt not in {"auto", "hex", "b64"}:
        print(f"Invalid --format: {fmt} (expected auto|hex|b64)")
        return 2

    try:
        payload, used_fmt = _decode_input(payload_arg, fmt)
    except ValueError as e:
        print(f"Invalid payload ({fmt}): {e}")
        return 2
    except base64.binascii.Error as e:
        print(f"Invalid payload ({fmt}): {e}")
        return 2

    if len(payload) != 11:
        print(f"Expected 11 bytes, got {len(payload)} bytes")
        print(f"HEX: {_fmt_hex(payload)}")
        return 2

    print("Decoded payload")
    print(f"- input_format: {used_fmt}")
    print(f"- raw_hex:    {_fmt_hex(payload)}")

    ts = int.from_bytes(payload[0:4], "big", signed=False)
    evt = payload[4]

    utc = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc)

    print(f"- layout:     standard ([0..3]=ts, [4]=evt)")
    print(f"- timestamp:  {ts} ({utc.isoformat()})")
    print(f"- event_id:   0x{evt:02X} ({EVT_NAMES.get(evt, 'UNKNOWN')})")

    if evt == 0x00:  # button
        button_id = payload[5]
        counter = (payload[6] << 16) | (payload[7] << 8) | payload[8]
        reserved = payload[9:11]
        print("- association: button_press")
        print(f"- button_id:  {button_id}")
        print(f"- counter:    {counter} (24-bit)")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x01:  # nfc in
        card_data = payload[5:9]
        reserved = payload[9:11]
        print("- association: nfc_check_in")
        print(f"- card_data:  0x{_fmt_hex(card_data)}")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x02:  # nfc out
        card_data = payload[5:9]
        reserved = payload[9:11]
        print("- association: nfc_check_out")
        print(f"- card_data:  0x{_fmt_hex(card_data)}")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x03:  # nfc vote
        button_id = payload[5]
        card_data = payload[6:10]
        reserved = payload[10:11]
        print("- association: nfc_vote")
        print(f"- button_id:  {button_id}")
        print(f"- card_data:  0x{_fmt_hex(card_data)}")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x10:  # battery status
        battery_mv = (payload[5] << 8) | payload[6]
        pct = payload[7]
        flags = payload[8]
        reserved = payload[9:11]
        print("- association: battery_status")
        print(f"- battery_mv: {battery_mv} mV")
        print(f"- percent:    {pct}%")
        print(f"- flags:      0x{flags:02X}")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x12:  # counter sync
        button_id = payload[5]
        counter = (payload[6] << 16) | (payload[7] << 8) | payload[8]
        reserved = payload[9:11]
        print("- association: counter_sync")
        print(f"- button_id:  {button_id}")
        print(f"- counter:    {counter} (24-bit)")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x13:  # device state snapshot (HK fport 20)
        last_cleaned = int.from_bytes(payload[5:9], "big", signed=False)
        tz = int.from_bytes(payload[9:11], "big", signed=True)
        print("- association: device_state_snapshot")
        utc = datetime.datetime.fromtimestamp(last_cleaned, tz=datetime.timezone.utc)
        print(f"- last_cleaned_epoch_utc: {last_cleaned} ({utc.isoformat()})")
   
        print(f"- tz_offset_minutes: {tz}")
    elif evt == 0x14:  # fw/hw version (DL 0x08 query, fport 21)
        fw = f"{payload[5]}.{payload[6]}.{payload[7]}"
        hw = f"{payload[8]}.{payload[9]}.{payload[10]}"
        print("- association: device_version_info")
        print(f"- fw_version: {fw}")
        print(f"- hw_version: {hw}")
    else:
        tail = payload[5:]
        print("- association: unknown")
        print(f"- tail:       0x{_fmt_hex(tail)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
