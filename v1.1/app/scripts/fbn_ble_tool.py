#!/usr/bin/env python3
"""
FeedbackNow (FBN) BLE Diagnostics & Management CLI Tool.

Supports:
  - Auto-discovering FBN-Flexbox devices over BLE
  - Reading and decoding Live Diagnostics (Battery, RTC, LoRa, Counters, HW/FW info)
  - Streaming real-time notifications on button presses
  - Reading Active Configuration (DevEUI, JoinEUI, AppKey, Unit ID, Region)
  - Syncing device RTC time with host system time
  - Triggering Control Commands (Clear counters, Refresh display, Test LoRa uplink)

Usage:
  python3 fbn_ble_tool.py diag                     # Auto-scan and read diagnostics once
  python3 fbn_ble_tool.py monitor                  # Stream live diagnostics & button presses
  python3 fbn_ble_tool.py config                   # Read active provisioning and LoRa keys
  python3 fbn_ble_tool.py sync-rtc                 # Sync device clock with PC UTC time
  python3 fbn_ble_tool.py clear-counters           # Reset button counters to 0
  python3 fbn_ble_tool.py refresh-display          # Force e-paper display refresh
  python3 fbn_ble_tool.py test-uplink              # Trigger LoRaWAN test uplink
  python3 fbn_ble_tool.py --address F0:2D:C5:...   # Connect to specific MAC address
"""

import argparse
import asyncio
import struct
import sys
import time
from typing import Optional
from bleak import BleakScanner, BleakClient
from bleak.backends.device import BLEDevice

# FeedbackNow Custom 128-bit UUIDs
UUID_FBN_SERVICE = "46424e00-0001-4000-8000-00805f9b34fb"
UUID_FBN_CONFIG  = "46424e01-0001-4000-8000-00805f9b34fb"
UUID_FBN_DIAG    = "46424e02-0001-4000-8000-00805f9b34fb"
UUID_FBN_CTRL    = "46424e03-0001-4000-8000-00805f9b34fb"

# Standard Device Information Service UUIDs
UUID_DIS_MANUF   = "00002a29-0000-1000-8000-00805f9b34fb"
UUID_DIS_MODEL   = "00002a24-0000-1000-8000-00805f9b34fb"
UUID_DIS_FW_REV  = "00002a26-0000-1000-8000-00805f9b34fb"

# Region lookup
REGION_MAP = {1: "US915", 2: "EU868", 3: "AU915", 4: "AS923"}
HW_VARIANT_MAP = {0: "Flexbox Standard", 1: "Flexbox Plus", 2: "Flexbox Plus Med"}
BOOT_CAUSE_MAP = {0: "Cold / Power-On Reset", 1: "Software Reset", 2: "Watchdog Reset", 3: "Brownout / Low Voltage"}


def decode_diagnostics(raw: bytes) -> dict:
    """Decode raw bytes from Diagnostics Characteristic into a dictionary."""
    if len(raw) >= 49:
        # 6-button format (49 bytes)
        bat_mv, rtc_ep, uptime, joined, margin, gateways, samples = struct.unpack_from("<HIIBhBH", raw, 0)
        btn_counts = list(struct.unpack_from("<6I", raw, 16))
        last_clean, boot_cause, hw_var, fw_maj, fw_min, fw_pat = struct.unpack_from("<IBBBBB", raw, 40)
    elif len(raw) >= 45:
        # 5-button format legacy fallback (45 bytes)
        bat_mv, rtc_ep, uptime, joined, margin, gateways, samples = struct.unpack_from("<HIIBhBH", raw, 0)
        btn_counts = list(struct.unpack_from("<5I", raw, 16)) + [0]
        last_clean, boot_cause, hw_var, fw_maj, fw_min, fw_pat = struct.unpack_from("<IBBBBB", raw, 36)
    else:
        return {"error": f"Payload too short ({len(raw)} bytes, expected >= 45)"}

    return {
        "battery_mv": bat_mv,
        "battery_v": bat_mv / 1000.0,
        "rtc_epoch": rtc_ep,
        "rtc_datetime": time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(rtc_ep)) if rtc_ep else "Not Synced",
        "uptime_seconds": uptime,
        "lora_joined": bool(joined),
        "last_demod_margin_db": margin if margin != -32768 else "N/A",
        "last_nb_gateways": gateways,
        "link_check_samples": samples,
        "button_counts": btn_counts,
        "last_cleaned_epoch": last_clean,
        "last_cleaned_datetime": time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(last_clean)) if last_clean else "None",
        "boot_cause": BOOT_CAUSE_MAP.get(boot_cause, f"Unknown ({boot_cause})"),
        "hw_variant": HW_VARIANT_MAP.get(hw_var, f"Unknown ({hw_var})"),
        "fw_version": f"{fw_maj}.{fw_min}.{fw_pat}",
    }


def print_diagnostics_table(data: dict):
    """Print decoded diagnostics formatted nicely."""
    print("\n" + "=" * 62)
    print("           FEEDBACKNOW LIVE DIAGNOSTICS TELEMETRY")
    print("=" * 62)
    print(f"  Firmware Version  : v{data['fw_version']}")
    print(f"  Hardware Variant  : {data['hw_variant']}")
    print(f"  Last Boot Cause   : {data['boot_cause']}")
    print(f"  Battery Voltage   : {data['battery_mv']} mV ({data['battery_v']:.3f} V)")
    print(f"  RTC Time (UTC)    : {data['rtc_datetime']} (epoch: {data['rtc_epoch']})")
    print(f"  System Uptime     : {data['uptime_seconds']} seconds ({data['uptime_seconds'] // 60}m {data['uptime_seconds'] % 60}s)")
    print("-" * 62)
    print(f"  LoRaWAN Network   : {'CONNECTED / JOINED' if data['lora_joined'] else 'DISCONNECTED / JOINING'}")
    print(f"  LoRa Demod Margin : {data['last_demod_margin_db']} dB")
    print(f"  Gateways In Range : {data['last_nb_gateways']}")
    print(f"  LinkCheck Samples : {data['link_check_samples']}")
    print("-" * 62)
    print(f"  Button Press Counters ({len(data['button_counts'])} buttons):")
    for i, count in enumerate(data['button_counts'], 1):
        print(f"    - Button {i}: {count}")
    print(f"  Last Cleaned (NFC): {data['last_cleaned_datetime']}")
    print("=" * 62 + "\n")


def decode_config(raw: bytes) -> dict:
    """Decode raw bytes from Configuration Characteristic into a dictionary."""
    if len(raw) < 80:
        return {"error": f"Payload too short ({len(raw)} bytes)"}

    magic, ver, prov, reg = struct.unpack_from("<BBBB", raw, 0)
    dev_eui = raw[4:12].hex().upper()
    join_eui = raw[12:20].hex().upper()
    app_key = raw[20:36].hex().upper()
    unit_id = raw[36:52].split(b'\x00')[0].decode('utf-8', errors='ignore')
    client = raw[52:76].split(b'\x00')[0].decode('utf-8', errors='ignore')
    epoch = struct.unpack_from("<I", raw, 76)[0]

    return {
        "magic_valid": magic == 0xFB,
        "version": ver,
        "is_provisioned": bool(prov),
        "storage_source": "EEPROM (Provisioned)" if prov else "Compile-Time Defaults",
        "lora_region": REGION_MAP.get(reg, f"Unknown ({reg})"),
        "dev_eui": dev_eui,
        "join_eui": join_eui,
        "app_key": app_key,
        "unit_id": unit_id,
        "client_name": client,
        "provision_epoch": epoch,
        "provision_datetime": time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(epoch)) if epoch else "N/A",
    }


def print_config_table(cfg: dict):
    """Print decoded configuration formatted nicely."""
    print("\n" + "=" * 62)
    print("           FEEDBACKNOW DEVICE CONFIGURATION")
    print("=" * 62)
    print(f"  Unit Identifier   : {cfg['unit_id']}")
    print(f"  Client / Location : {cfg['client_name']}")
    print(f"  Config Source     : {cfg['storage_source']}")
    print(f"  LoRa Region       : {cfg['lora_region']}")
    print("-" * 62)
    print(f"  LoRa DevEUI       : {cfg['dev_eui']}")
    print(f"  LoRa JoinEUI      : {cfg['join_eui']}")
    print(f"  LoRa AppKey       : {cfg['app_key']}")
    print(f"  Provision Date    : {cfg['provision_datetime']}")
    print("=" * 62 + "\n")


async def find_device(target_addr: Optional[str] = None) -> Optional[BLEDevice]:
    """Scan for FBN-Flexbox device or matching address."""
    print("[*] Scanning for FeedbackNow BLE devices (timeout: 7s)...")
    devices = await BleakScanner.discover(timeout=7.0, return_adv=True)

    for dev, adv in devices.values():
        name = adv.local_name or dev.name or ""
        addr = dev.address
        if target_addr and addr.upper() == target_addr.upper():
            print(f"[+] Found specified device {addr} (RSSI: {adv.rssi} dBm)")
            return dev
        if "FBN" in name or "Flexbox" in name or UUID_FBN_SERVICE.lower() in [u.lower() for u in adv.service_uuids]:
            print(f"[+] Found '{name}' at {addr} (RSSI: {adv.rssi} dBm)")
            return dev

    if target_addr:
        # Fallback to direct device instance if not seen in scan
        return await BleakScanner.find_device_by_address(target_addr, timeout=5.0)

    return None


async def cmd_diagnostics(address: Optional[str]):
    device = await find_device(address)
    if not device:
        print("[-] Error: No FeedbackNow device found. Make sure advertising is active.")
        return

    print(f"[*] Connecting to {device.address}...")
    async with BleakClient(device) as client:
        print("[+] Connected! Reading live diagnostics...")
        raw = await client.read_gatt_char(UUID_FBN_DIAG)
        diag = decode_diagnostics(raw)
        print_diagnostics_table(diag)


async def cmd_monitor(address: Optional[str]):
    device = await find_device(address)
    if not device:
        print("[-] Error: No FeedbackNow device found.")
        return

    print(f"[*] Connecting to {device.address} for live streaming...")
    async with BleakClient(device) as client:
        print("[+] Connected! Subscribing to live notifications (Press Ctrl+C to stop)...")

        # Read once initially
        initial_raw = await client.read_gatt_char(UUID_FBN_DIAG)
        diag = decode_diagnostics(initial_raw)
        print_diagnostics_table(diag)

        def notification_handler(sender, data: bytearray):
            updated = decode_diagnostics(bytes(data))
            t = time.strftime('%H:%M:%S')
            counts = updated['button_counts']
            print(f"[{t}] Notification: Bat={updated['battery_mv']}mV | Buttons=[{counts[0]}, {counts[1]}, {counts[2]}, {counts[3]}, {counts[4]}] | Joined={updated['lora_joined']}")

        await client.start_notify(UUID_FBN_DIAG, notification_handler)
        print("[*] Listening for button presses and telemetry events...")
        try:
            while True:
                await asyncio.sleep(1.0)
        except asyncio.CancelledError:
            pass
        finally:
            await client.stop_notify(UUID_FBN_DIAG)


async def cmd_config(address: Optional[str]):
    device = await find_device(address)
    if not device:
        print("[-] Error: No FeedbackNow device found.")
        return

    print(f"[*] Connecting to {device.address}...")
    async with BleakClient(device) as client:
        print("[+] Connected! Reading configuration...")
        raw = await client.read_gatt_char(UUID_FBN_CONFIG)
        cfg = decode_config(raw)
        print_config_table(cfg)


async def cmd_control(address: Optional[str], opcode: int, name: str):
    device = await find_device(address)
    if not device:
        print("[-] Error: No FeedbackNow device found.")
        return

    print(f"[*] Connecting to {device.address}...")
    async with BleakClient(device) as client:
        print(f"[+] Connected! Sending {name} command (Opcode: 0x{opcode:02X})...")
        await client.write_gatt_char(UUID_FBN_CTRL, bytes([opcode]), response=True)
        print(f"[+] Command {name} executed successfully!")


async def cmd_sync_rtc(address: Optional[str]):
    device = await find_device(address)
    if not device:
        print("[-] Error: No FeedbackNow device found.")
        return

    now_epoch = int(time.time())
    # Command 0x03 on Config Characteristic: [0x03, epoch uint32 little endian]
    payload = struct.pack("<BI", 0x03, now_epoch)

    print(f"[*] Connecting to {device.address}...")
    async with BleakClient(device) as client:
        print(f"[+] Connected! Syncing RTC to {now_epoch} ({time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(now_epoch))})...")
        await client.write_gatt_char(UUID_FBN_CONFIG, payload, response=True)
        print("[+] RTC synchronized successfully!")


def main():
    parser = argparse.ArgumentParser(description="FeedbackNow BLE Tool")
    parser.add_argument("command", choices=[
        "diag", "monitor", "config", "sync-rtc",
        "clear-counters", "refresh-display", "test-uplink", "reboot"
    ], help="Action to perform")
    parser.add_argument("-a", "--address", default=None, help="Target BLE MAC address (e.g. F0:2D:C5:98:4F:8B)")

    args = parser.parse_args()
    cmd = args.command
    addr = args.address

    if cmd == "diag":
        asyncio.run(cmd_diagnostics(addr))
    elif cmd == "monitor":
        try:
            asyncio.run(cmd_monitor(addr))
        except KeyboardInterrupt:
            print("\n[*] Monitoring stopped by user.")
    elif cmd == "config":
        asyncio.run(cmd_config(addr))
    elif cmd == "sync-rtc":
        asyncio.run(cmd_sync_rtc(addr))
    elif cmd == "clear-counters":
        asyncio.run(cmd_control(addr, 0x03, "Clear Counters"))
    elif cmd == "refresh-display":
        asyncio.run(cmd_control(addr, 0x04, "Refresh Display"))
    elif cmd == "test-uplink":
        asyncio.run(cmd_control(addr, 0x02, "Test LoRa Uplink"))
    elif cmd == "reboot":
        asyncio.run(cmd_control(addr, 0x05, "Reboot Device"))


if __name__ == "__main__":
    main()
