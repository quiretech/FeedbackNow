#!/usr/bin/env python3
"""Parse Flexbox DevEUIs and 11-byte LoRa payloads used by firmware payload_gen.

Usage:
  python3 parse_flexbox.py <INPUT>
  python3 parse_flexbox.py --deveui <DEV_EUI>
  python3 parse_flexbox.py --format {auto,hex,b64} <PAYLOAD>

Auto-detect:
  - 16 hex chars (optional colons) → DevEUI heartbeat lookup
  - 22 hex chars or 11-byte base64 → uplink payload decode

DevEUI heartbeat (FRD 4.9, matches app/src/housekeeping.c):
  offset_minutes = (DevEUI[7]×256 + DevEUI[6]) % 1440
  daily heartbeat UTC = 00:00 UTC + offset_minutes

Examples:
  python3 parse_flexbox.py 464c58d039797cf4
  python3 parse_flexbox.py 6945A731000400000A0000
  python3 parse_flexbox.py "aUWnMQAEAAAKAAA="
  python3 parse_flexbox.py --format b64 "<aUWnMQAEAAAKAAA=>"

Payload format (11 bytes):
  Default: [0..3] timestamp (epoch seconds, big-endian), [4] event type, [5..] event-specific
  Exception — 0x14 (FPort 21, DL 0x08): [0]=0x14, no leading ts; see Event types below.

Event types (from app/include/payload_gen.h):
  0x00 = EVT_BUTTON:         [5]=button_id, [6..8]=counter (24-bit BE), [9..10]=reserved
  0x01 = EVT_NFC_IN:         [5..8]=card_data_4, [9..10]=reserved
  0x02 = EVT_NFC_OUT:        [5..8]=card_data_4, [9..10]=reserved
  0x03 = EVT_NFC_VOTE:       [5]=button_id, [6..9]=card_data_4, [10]=reserved
  0x10 = EVT_BATTERY_STATUS: [5..6]=battery_mv (16-bit BE), [7]=percent, [8]=flags, [9..10]=reserved
  0x12 = EVT_COUNTER_SYNC:   [5]=button_id, [6..8]=counter (24-bit BE), [9..10]=reserved
  0x13 = EVT_DEVICE_STATE_SNAPSHOT (FPort 20 HK: post-join, daily HK, DL 0x04): [0..3]=ts BE,
         [4]=0x13, [5..8]=last_cleaned_epoch BE (EEPROM), [9..10]=tz_offset_min int16 BE
  0x14 = EVT_DEVICE_VERSION_INFO (FPort 21, DL 0x08): **no leading ts** — [0]=0x14,
         [1..3]=fw maj/min/patch, [4..6]=hw maj/min/patch, [7]=1 if EPD build else 0,
         [8..10]=reserved 0
"""

from __future__ import annotations

import base64
import datetime
import re
import sys
from zoneinfo import ZoneInfo

UTC = datetime.timezone.utc
NYC = ZoneInfo("America/New_York")
SECONDS_PER_DAY = 86_400
MINUTES_PER_DAY = 1_440

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
    s = re.sub(r"[^0-9a-fA-F]", "", s)
    return s


def _clean_b64(s: str) -> str:
    s = _clean_wrapped(s)
    s = "".join(s.split())
    return s


def _fmt_hex(b: bytes) -> str:
    return "".join(f"{x:02X}" for x in b)


def _parse_hex(s: str) -> bytes:
    cleaned = _clean_hex(s)
    if not cleaned:
        raise ValueError("empty hex input")
    if len(cleaned) % 2 != 0:
        raise ValueError(f"odd hex length: {len(cleaned)}")
    return bytes.fromhex(cleaned)


def _parse_b64(s: str) -> bytes:
    raw = _clean_b64(s)
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

    # A '=' character can only appear in base64 padding — never in hex.
    # Also, if the raw (non-hex-stripped) string length matches 22 hex chars for
    # an 11-byte payload, treat as hex. Otherwise fall through to base64.
    stripped = _clean_wrapped(s)
    if "=" in stripped or "+" in stripped or "/" in stripped:
        return _parse_b64(s), "b64"
    cleaned = _clean_hex(s)
    if cleaned and len(cleaned) == len(stripped.replace(" ", "").replace(":", "")):
        # All chars were valid hex — no letters were silently dropped
        try:
            return _parse_hex(s), "hex"
        except ValueError:
            pass
    return _parse_b64(s), "b64"


def _looks_like_deveui(s: str) -> bool:
    cleaned = _clean_hex(s)
    return len(cleaned) == 16 and bool(re.fullmatch(r"[0-9a-fA-F]{16}", cleaned))


def _heartbeat_offset_minutes(dev_eui: bytes) -> int:
    if len(dev_eui) != 8:
        raise ValueError(f"DevEUI must be 8 bytes, got {len(dev_eui)}")
    return (dev_eui[7] * 256 + dev_eui[6]) % MINUTES_PER_DAY


def _format_hhmm(minutes: int) -> str:
    return f"{minutes // 60:02d}:{minutes % 60:02d}"


def _format_delta(delta: datetime.timedelta) -> str:
    total_sec = int(abs(delta.total_seconds()))
    days, rem = divmod(total_sec, SECONDS_PER_DAY)
    hours, rem = divmod(rem, 3600)
    minutes, seconds = divmod(rem, 60)
    parts: list[str] = []
    if days:
        parts.append(f"{days}d")
    if hours:
        parts.append(f"{hours}h")
    if minutes:
        parts.append(f"{minutes}m")
    if seconds or not parts:
        parts.append(f"{seconds}s")
    return " ".join(parts)


def _tz_offset_label(dt: datetime.datetime) -> str:
    offset = dt.utcoffset()
    if offset is None:
        return "UTC"
    total_min = int(offset.total_seconds() // 60)
    sign = "+" if total_min >= 0 else "-"
    total_min = abs(total_min)
    return f"UTC{sign}{total_min // 60:02d}:{total_min % 60:02d}"


def _next_and_last_heartbeat(
    now_utc: datetime.datetime, offset_minutes: int
) -> tuple[datetime.datetime, datetime.datetime]:
    midnight = datetime.datetime.combine(now_utc.date(), datetime.time(0, 0), tzinfo=UTC)
    offset = datetime.timedelta(minutes=offset_minutes)
    today_hb = midnight + offset
    if now_utc < today_hb:
        next_hb = today_hb
        last_hb = today_hb - datetime.timedelta(days=1)
    else:
        next_hb = today_hb + datetime.timedelta(days=1)
        last_hb = today_hb
    return next_hb, last_hb


def parse_deveui(dev_eui: bytes) -> int:
    offset_minutes = _heartbeat_offset_minutes(dev_eui)
    now_utc = datetime.datetime.now(tz=UTC)
    now_nyc = now_utc.astimezone(NYC)
    next_hb_utc, last_hb_utc = _next_and_last_heartbeat(now_utc, offset_minutes)
    next_hb_nyc = next_hb_utc.astimezone(NYC)
    last_hb_nyc = last_hb_utc.astimezone(NYC)
    hb_utc_today = datetime.datetime.combine(
        now_utc.date(),
        datetime.time(hour=offset_minutes // 60, minute=offset_minutes % 60),
        tzinfo=UTC,
    )
    hb_nyc_today = hb_utc_today.astimezone(NYC)

    until_next = next_hb_utc - now_utc
    since_last = now_utc - last_hb_utc

    print("Flexbox DevEUI")
    print(f"- dev_eui:                 {_fmt_hex(dev_eui).lower()}")
    print(f"- dev_eui_bytes[6..7]:       0x{dev_eui[6]:02x} 0x{dev_eui[7]:02x}")
    print(
        "- offset_minutes:          "
        f"{offset_minutes} ({_format_hhmm(offset_minutes)} after 00:00 UTC)"
    )
    print(
        "- expected_heartbeat_utc:  "
        f"{_format_hhmm(offset_minutes)} UTC daily (FRD 4.9 jitter)"
    )
    print(
        "- expected_heartbeat_nyc:  "
        f"{hb_nyc_today.strftime('%H:%M')} {hb_nyc_today.tzname()} "
        f"({_tz_offset_label(hb_nyc_today)}) daily"
    )
    print(f"- now_utc:                 {now_utc.strftime('%Y-%m-%d %H:%M:%S %Z')}")
    print(
        "- now_nyc:                 "
        f"{now_nyc.strftime('%Y-%m-%d %H:%M:%S %Z')} ({_tz_offset_label(now_nyc)})"
    )
    print(
        "- nyc_to_utc_offset_now:   "
        f"NYC is {_tz_offset_label(now_nyc)} now "
        f"({-int(now_nyc.utcoffset().total_seconds() // 60)} min behind UTC)"
    )
    print(f"- next_heartbeat_utc:      {next_hb_utc.isoformat()} (in {_format_delta(until_next)})")
    print(
        "- next_heartbeat_nyc:      "
        f"{next_hb_nyc.strftime('%Y-%m-%d %H:%M:%S %Z')} ({_tz_offset_label(next_hb_nyc)})"
    )
    print(f"- last_heartbeat_utc:      {last_hb_utc.isoformat()} ({_format_delta(since_last)} ago)")
    print(
        "- last_heartbeat_nyc:      "
        f"{last_hb_nyc.strftime('%Y-%m-%d %H:%M:%S %Z')} ({_tz_offset_label(last_hb_nyc)})"
    )
    return 0


def parse_payload(payload: bytes, used_fmt: str) -> int:
    if len(payload) != 11:
        print(f"Expected 11 bytes, got {len(payload)} bytes")
        print(f"HEX: {_fmt_hex(payload)}")
        return 2

    print("Decoded payload")
    print(f"- input_format: {used_fmt}")
    print(f"- raw_hex:    {_fmt_hex(payload)}")

    if payload[0] == 0x14:
        fw = f"{payload[1]}.{payload[2]}.{payload[3]}"
        hw = f"{payload[4]}.{payload[5]}.{payload[6]}"
        epd = payload[7]
        reserved = payload[8:11]
        print("- layout:     version_info ([0]=0x14, no timestamp)")
        print(f"- event_id:   0x14 ({EVT_NAMES[0x14]})")
        print("- association: device_version_info")
        print(f"- fw_version: {fw}")
        print(f"- hw_version: {hw}")
        print(f"- epd_enabled: {epd} (0=no-EPD variant A, 1=EPD variant B)")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
        return 0

    ts = int.from_bytes(payload[0:4], "big", signed=False)
    evt = payload[4]

    utc = datetime.datetime.fromtimestamp(ts, tz=UTC)

    print(f"- layout:     standard ([0..3]=ts, [4]=evt)")
    print(f"- timestamp:  {ts} ({utc.isoformat()})")
    print(f"- event_id:   0x{evt:02X} ({EVT_NAMES.get(evt, 'UNKNOWN')})")

    if evt == 0x00:
        button_id = payload[5]
        counter = (payload[6] << 16) | (payload[7] << 8) | payload[8]
        reserved = payload[9:11]
        print("- association: button_press")
        print(f"- button_id:  {button_id}")
        print(f"- counter:    {counter} (24-bit)")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x01:
        card_data = payload[5:9]
        reserved = payload[9:11]
        print("- association: nfc_check_in")
        print(f"- card_data:  0x{_fmt_hex(card_data)}")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x02:
        card_data = payload[5:9]
        reserved = payload[9:11]
        print("- association: nfc_check_out")
        print(f"- card_data:  0x{_fmt_hex(card_data)}")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x03:
        button_id = payload[5]
        card_data = payload[6:10]
        reserved = payload[10:11]
        print("- association: nfc_vote")
        print(f"- button_id:  {button_id}")
        print(f"- card_data:  0x{_fmt_hex(card_data)}")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x10:
        battery_mv = (payload[5] << 8) | payload[6]
        pct = payload[7]
        flags = payload[8]
        reserved = payload[9:11]
        print("- association: battery_status")
        print(f"- battery_mv: {battery_mv} mV")
        print(f"- percent:    {pct}%")
        print(f"- flags:      0x{flags:02X}")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x12:
        button_id = payload[5]
        counter = (payload[6] << 16) | (payload[7] << 8) | payload[8]
        reserved = payload[9:11]
        print("- association: counter_sync")
        print(f"- button_id:  {button_id}")
        print(f"- counter:    {counter} (24-bit)")
        print(f"- reserved:   0x{_fmt_hex(reserved)}")
    elif evt == 0x13:
        last_cleaned = int.from_bytes(payload[5:9], "big", signed=False)
        tz = int.from_bytes(payload[9:11], "big", signed=True)
        print("- association: device_state_snapshot")
        cleaned_utc = datetime.datetime.fromtimestamp(last_cleaned, tz=UTC)
        print(f"- last_cleaned_epoch_utc: {last_cleaned} ({cleaned_utc.isoformat()})")
        print(f"- tz_offset_minutes: {tz}")
    else:
        tail = payload[5:]
        print("- association: unknown")
        print(f"- tail:       0x{_fmt_hex(tail)}")

    return 0


def main(argv: list[str]) -> int:
    if len(argv) < 2 or any(arg in {"-h", "--help"} for arg in argv[1:]):
        print(__doc__.rstrip())
        return 2

    fmt = "auto"
    mode = "auto"
    input_arg = ""
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
        if arg in {"--deveui", "--dev-eui"}:
            mode = "deveui"
            i += 1
            continue
        if arg in {"--payload"}:
            mode = "payload"
            i += 1
            continue
        if arg.startswith("-"):
            print(f"Unknown option: {arg}")
            return 2
        if input_arg:
            print("Unexpected extra argument")
            return 2
        input_arg = arg
        i += 1

    if not input_arg:
        print("Missing input argument")
        return 2
    if fmt not in {"auto", "hex", "b64"}:
        print(f"Invalid --format: {fmt} (expected auto|hex|b64)")
        return 2

    if mode == "deveui" or (mode == "auto" and _looks_like_deveui(input_arg)):
        try:
            dev_eui = _parse_hex(input_arg)
        except ValueError as e:
            print(f"Invalid DevEUI: {e}")
            return 2
        if len(dev_eui) != 8:
            print(f"DevEUI must be 8 bytes, got {len(dev_eui)} bytes")
            return 2
        return parse_deveui(dev_eui)

    try:
        payload, used_fmt = _decode_input(input_arg, fmt)
    except ValueError as e:
        print(f"Invalid payload ({fmt}): {e}")
        return 2
    except base64.binascii.Error as e:
        print(f"Invalid payload ({fmt}): {e}")
        return 2

    return parse_payload(payload, used_fmt)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
