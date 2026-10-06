#!/usr/bin/env python3
"""
Test script for FeedbackNow BLE Manager & Custom GATT Service.
Connects via BlueZ D-Bus, reads standard Device Information,
reads FeedbackNow Configuration and Live Diagnostics, and decodes the payload.
"""

import dbus
import dbus.mainloop.glib
from gi.repository import GLib
import struct
import sys
import time

DEV_ADDR = "F0:2D:C5:98:4F:8B"
DEV_PATH = f"/org/bluez/hci0/dev_{DEV_ADDR.replace(':', '_')}"

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
loop = GLib.MainLoop()

# Standard & Custom UUIDs
UUID_MANUF = "00002a29-0000-1000-8000-00805f9b34fb"
UUID_MODEL = "00002a24-0000-1000-8000-00805f9b34fb"
UUID_FWREV = "00002a26-0000-1000-8000-00805f9b34fb"

UUID_FBN_CONFIG = "46424e01-0001-4000-8000-00805f9b34fb"
UUID_FBN_DIAG   = "46424e02-0001-4000-8000-00805f9b34fb"
UUID_FBN_CTRL   = "46424e03-0001-4000-8000-00805f9b34fb"

try:
    dev_obj = bus.get_object('org.bluez', DEV_PATH)
    dev_props = dbus.Interface(dev_obj, 'org.freedesktop.DBus.Properties')
    dev_iface = dbus.Interface(dev_obj, 'org.bluez.Device1')
except Exception as e:
    print(f"Device object not found ({e}). Make sure the device has been scanned.")
    sys.exit(1)

def on_properties_changed(interface, changed, invalidated, path):
    if path == DEV_PATH and changed.get('ServicesResolved', False):
        print("[+] GATT Services resolved successfully!")
        GLib.timeout_add(500, read_and_decode_all)

bus.add_signal_receiver(
    on_properties_changed,
    dbus_interface="org.freedesktop.DBus.Properties",
    signal_name="PropertiesChanged",
    path_keyword="path"
)

def read_and_decode_all():
    manager = dbus.Interface(bus.get_object('org.bluez', '/'), 'org.freedesktop.DBus.ObjectManager')
    objects = manager.GetManagedObjects()

    chars = {}
    for p, ifaces in objects.items():
        if DEV_PATH in p and 'org.bluez.GattCharacteristic1' in ifaces:
            u = str(ifaces['org.bluez.GattCharacteristic1']['UUID']).lower()
            chars[u] = ifaces['org.bluez.GattCharacteristic1']
            chars[u]['_path'] = p

    print("\n" + "=" * 60)
    print(" 1. STANDARD DEVICE INFORMATION SERVICE (DIS)")
    print("=" * 60)

    for name, uuid in [("Manufacturer", UUID_MANUF), ("Model Number", UUID_MODEL), ("Firmware Rev", UUID_FWREV)]:
        if uuid in chars:
            c_obj = bus.get_object('org.bluez', chars[uuid]['_path'])
            c_iface = dbus.Interface(c_obj, 'org.bluez.GattCharacteristic1')
            val = bytes(c_iface.ReadValue({})).decode('utf-8', errors='ignore')
            print(f"  * {name:<16}: {val}")

    print("\n" + "=" * 60)
    print(" 2. FEEDBACKNOW CONFIGURATION CHARACTERISTIC")
    print("=" * 60)
    if UUID_FBN_CONFIG in chars:
        c_obj = bus.get_object('org.bluez', chars[UUID_FBN_CONFIG]['_path'])
        c_iface = dbus.Interface(c_obj, 'org.bluez.GattCharacteristic1')
        raw = bytes(c_iface.ReadValue({}))
        print(f"  * Raw bytes ({len(raw)} B): {raw.hex()}")

        # Unpack struct fbn_ble_config_read_resp
        # uint8 magic, uint8 version, uint8 is_provisioned, uint8 lora_region,
        # uint8 dev_eui[8], uint8 join_eui[8], uint8 app_key[16],
        # char unit_id[16], char client_name[24], uint32 provision_epoch
        if len(raw) >= 80:
            magic, ver, prov, reg = struct.unpack_from("<BBBB", raw, 0)
            dev_eui = raw[4:12].hex()
            join_eui = raw[12:20].hex()
            app_key = raw[20:36].hex()
            unit_id = raw[36:52].split(b'\x00')[0].decode('utf-8', errors='ignore')
            client = raw[52:76].split(b'\x00')[0].decode('utf-8', errors='ignore')
            epoch = struct.unpack_from("<I", raw, 76)[0]

            region_str = {1: "US915", 2: "EU868", 3: "AU915", 4: "AS923"}.get(reg, f"Unknown ({reg})")
            print(f"  * Magic           : 0x{magic:02X} (Valid: {magic == 0xFB})")
            print(f"  * Version         : {ver}")
            print(f"  * Storage Source  : {'EEPROM Provisioned' if prov else 'Compile-time Defaults'}")
            print(f"  * Unit ID         : {unit_id}")
            print(f"  * Client Tag      : {client}")
            print(f"  * LoRa Region     : {region_str}")
            print(f"  * LoRa DevEUI     : {dev_eui}")
            print(f"  * LoRa JoinEUI    : {join_eui}")
            print(f"  * LoRa AppKey     : {app_key}")
            print(f"  * Provision Epoch : {epoch}")

    print("\n" + "=" * 60)
    print(" 3. FEEDBACKNOW LIVE DIAGNOSTICS CHARACTERISTIC")
    print("=" * 60)
    if UUID_FBN_DIAG in chars:
        c_obj = bus.get_object('org.bluez', chars[UUID_FBN_DIAG]['_path'])
        c_iface = dbus.Interface(c_obj, 'org.bluez.GattCharacteristic1')
        raw = bytes(c_iface.ReadValue({}))
        print(f"  * Raw bytes ({len(raw)} B): {raw.hex()}")

        # struct fbn_ble_diag_read_resp
        # uint16 battery_mv, uint32 rtc_epoch, uint32 uptime_seconds,
        # uint8 lora_joined, int16 last_demod_margin, uint8 last_nb_gateways,
        # uint16 link_check_samples, uint32 button_counts[5],
        # uint32 last_cleaned_epoch, uint8 boot_cause, uint8 hw_variant,
        # uint8 fw_major, uint8 fw_minor, uint8 fw_patch
        if len(raw) >= 49:
            bat_mv, rtc_ep, uptime, joined, margin, gateways, samples = struct.unpack_from("<HIIBhBH", raw, 0)
            btn_counts = struct.unpack_from("<6I", raw, 16)
            last_clean, boot_cause, hw_var, fw_maj, fw_min, fw_pat = struct.unpack_from("<IBBBBB", raw, 40)
        elif len(raw) >= 45:
            bat_mv, rtc_ep, uptime, joined, margin, gateways, samples = struct.unpack_from("<HIIBhBH", raw, 0)
            btn_counts = list(struct.unpack_from("<5I", raw, 16)) + [0]
            last_clean, boot_cause, hw_var, fw_maj, fw_min, fw_pat = struct.unpack_from("<IBBBBB", raw, 36)
        else:
            btn_counts = []

        if len(raw) >= 45:
            hw_str = {0: "FLEXBOX", 1: "FLEXBOX_PLUS", 2: "FLEXBOX_PLUS_MED"}.get(hw_var, f"Unknown ({hw_var})")
            print(f"  * Battery Voltage : {bat_mv} mV ({bat_mv/1000:.3f} V)")
            print(f"  * RTC Epoch       : {rtc_ep} ({time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(rtc_ep)) if rtc_ep else 'Not synced'})")
            print(f"  * System Uptime   : {uptime} s")
            print(f"  * LoRaWAN State   : {'JOINED' if joined else 'NOT JOINED / JOINING'}")
            print(f"  * Link Margin     : {margin} dB (Gateways: {gateways}, Samples: {samples})")
            print(f"  * Button Counters : " + " | ".join(f"{i+1}:{c}" for i, c in enumerate(btn_counts)))
            print(f"  * Last Cleaned    : {last_clean}")
            print(f"  * Boot Cause Code : {boot_cause}")
            print(f"  * HW Variant      : {hw_str}")
            print(f"  * Firmware Vers.  : v{fw_maj}.{fw_min}.{fw_pat}")

    print("\n" + "=" * 60)
    print(" Disconnecting...")
    dev_iface.Disconnect()
    print(" Test Complete! Exiting.")
    print("=" * 60)
    loop.quit()
    return False

def connect_timeout():
    print("[-] Timeout waiting for connection or services.")
    loop.quit()
    return False

print(f"Connecting to {DEV_ADDR}...")
try:
    dev_iface.Connect()
    print("[+] Connect() issued, waiting for GATT service discovery...")
    GLib.timeout_add(15000, connect_timeout)
except Exception as e:
    print(f"[-] Connect() error: {e}")
    sys.exit(1)

loop.run()
