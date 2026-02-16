#!/usr/bin/env python3
"""Parse the 11-byte LoRa payload used by zsk/lora.

Usage:
  python3 parse_payload.py <HEX>

Examples:
  python3 parse_payload.py 6945A731000400000A0000
  python3 parse_payload.py <6945A731000400000A0000>

Format (11 bytes):
  [0..3] timestamp (epoch seconds, big-endian)
  [4]    event type
  [5..]  event-specific

Event types (from src/payload_gen.h):
  0x00 = BUTTON: [5]=button_id, [6..8]=counter (24-bit BE), [9..10]=reserved
  0x01 = NFC:    [5..10]=uid (6 bytes)
  0x02 = BATT:   [5]=percent (0..100), [6..10]=reserved
"""

from __future__ import annotations

import datetime
import sys


EVT_NAMES = {
    0x00: "EVT_BUTTON",
    0x01: "EVT_NFC",
    0x02: "EVT_BATTERY",
    0xFF: "EVT_FUTURE",
}


def _clean_hex(s: str) -> str:
    s = s.strip()
    if s.startswith("<") and s.endswith(">"):
        s = s[1:-1].strip()
    if s.startswith("0x") or s.startswith("0X"):
        s = s[2:]
    s = "".join(s.split())
    return s


def _fmt_hex(b: bytes) -> str:
    return "".join(f"{x:02X}" for x in b)


def main(argv: list[str]) -> int:
    if len(argv) != 2 or argv[1] in {"-h", "--help"}:
        print(__doc__.rstrip())
        return 2

    hexstr = _clean_hex(argv[1])
    try:
        payload = bytes.fromhex(hexstr)
    except ValueError as e:
        print(f"Invalid hex: {e}")
        return 2

    if len(payload) != 11:
        print(f"Expected 11 bytes, got {len(payload)} bytes")
        print(f"HEX: {_fmt_hex(payload)}")
        return 2

    ts = int.from_bytes(payload[0:4], "big", signed=False)
    evt = payload[4]

    utc = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc)

    print("Decoded payload")
    print(f"- raw_hex:    {_fmt_hex(payload)}")
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
    elif evt == 0x01:  # nfc
        uid = payload[5:11]
        print("- association: nfc_scan")
        print(f"- uid:        {_fmt_hex(uid)}")
    elif evt == 0x02:  # battery
        pct = payload[5]
        reserved = payload[6:11]
        print("- association: battery")
        print(f"- percent:    {pct}%")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    else:
        tail = payload[5:]
        print("- association: unknown")
        print(f"- tail:       0x{_fmt_hex(tail)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
