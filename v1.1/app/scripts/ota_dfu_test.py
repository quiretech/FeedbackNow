#!/usr/bin/env python3
"""
FeedbackNow Over-the-Air (OTA) DFU Automated Test Tool.
Uses SMP over BLE to upload a new signed firmware image to Slot 1,
sets it for testing/overwrite, reboots the target, and verifies that
the device successfully boots the new version in Slot 0.
"""

import argparse
import asyncio
import os
import subprocess
import sys
import time
from smpclient import SMPClient
from smpclient.transport.ble import SMPBLETransport
from smpclient.requests.image_management import ImageStatesRead, ImageStatesWrite
from smpclient.requests.os_management import ResetWrite

DEFAULT_ADDR = "F0:2D:C5:98:4F:8B"
DEFAULT_BIN = "build/app/zephyr/zephyr.signed.bin"


def disconnect_bluez(addr: str):
    """Ensure any lingering BlueZ connection is dropped so SMP can connect cleanly."""
    try:
        subprocess.run(["bluetoothctl", "disconnect", addr],
                       capture_output=True, timeout=5)
        time.sleep(1.0)
    except Exception:
        pass


async def run_ota(address: str, bin_path: str):
    if not os.path.isfile(bin_path):
        print(f"[-] Error: Image file '{bin_path}' not found!")
        sys.exit(1)

    with open(bin_path, "rb") as f:
        image_bytes = f.read()

    total_bytes = len(image_bytes)
    print(f"[*] Loaded image: {bin_path} ({total_bytes:,} bytes)")

    print(f"[*] Ensuring clean BLE link state with {address}...")
    disconnect_bluez(address)

    transport = SMPBLETransport()
    client = SMPClient(transport, address=address)

    print(f"[*] Connecting to {address} over BLE...")
    try:
        await client.connect(connect_timeout_s=25.0)
    except Exception as e:
        print(f"[-] Connection failed: {e}")
        sys.exit(1)

    print("[+] Connected to SMP service!")

    # 1. Read initial image state
    print("[*] Reading initial image slots...")
    initial_states = await client.request(ImageStatesRead())
    print("------------------------------------------------------------")
    for img in initial_states.images:
        status = []
        if img.active: status.append("ACTIVE")
        if img.confirmed: status.append("CONFIRMED")
        if img.pending: status.append("PENDING")
        if img.bootable: status.append("BOOTABLE")
        print(f"  Slot #{img.slot}: v{img.version} | Hash: {img.hash.hex()[:16]}... | [{', '.join(status)}]")
    print("------------------------------------------------------------")

    # 2. Upload the new image
    print(f"[*] Starting OTA upload of {total_bytes:,} bytes to device...")
    start_time = time.time()
    last_print = 0

    try:
        async for offset in client.upload(image_bytes, slot=0):
            pct = (offset / total_bytes) * 100
            elapsed = time.time() - start_time
            speed = (offset / 1024) / elapsed if elapsed > 0 else 0
            # Print update every ~5% or at end
            if pct - last_print >= 5 or offset == total_bytes:
                print(f"  --> Progress: {offset:,}/{total_bytes:,} bytes ({pct:.1f}%) @ {speed:.1f} KB/s")
                last_print = pct
    except Exception as e:
        print(f"[-] Upload failed: {e}")
        await client.disconnect()
        sys.exit(1)

    elapsed = time.time() - start_time
    avg_speed = (total_bytes / 1024) / elapsed if elapsed > 0 else 0
    print(f"[+] Upload complete in {elapsed:.1f}s (Average speed: {avg_speed:.1f} KB/s)")

    # 3. Read image states after upload
    print("[*] Verifying uploaded image in secondary slot...")
    post_upload = await client.request(ImageStatesRead())
    slot1_hash = None
    for img in post_upload.images:
        print(f"  Slot #{img.slot}: v{img.version} | Hash: {img.hash.hex()[:16]}... | Bootable: {img.bootable}")
        if img.slot == 1:
            slot1_hash = img.hash

    if not slot1_hash:
        print("[-] Error: Image was not found in Slot #1!")
        await client.disconnect()
        sys.exit(1)

    # 4. Set test/swap state
    print(f"[*] Marking Slot #1 for test upgrade (Hash: {slot1_hash.hex()[:16]}...)...")
    write_resp = await client.request(ImageStatesWrite(hash=slot1_hash, confirm=False))
    print(f"[+] State write response: {write_resp}")

    # 5. Issue system reboot
    print("[*] Sending system reset command...")
    try:
        await client.request(ResetWrite())
    except Exception:
        # Reset terminates connection immediately, which is expected
        pass

    try:
        await client.disconnect()
    except Exception:
        pass

    print("[+] Device reset command sent! Waiting for MCUboot to overwrite and boot (15s)...")
    await asyncio.sleep(15.0)

    # 6. Reconnect and verify new version
    print("[*] Reconnecting to device to verify upgrade...")
    disconnect_bluez(address)

    transport_verify = SMPBLETransport()
    client_verify = SMPClient(transport_verify, address=address)

    for attempt in range(1, 4):
        try:
            print(f"  Attempt {attempt}/3 to connect...")
            await client_verify.connect(connect_timeout_s=15.0)
            break
        except Exception as e:
            print(f"  Attempt {attempt} failed ({e}), retrying in 3s...")
            await asyncio.sleep(3.0)
    else:
        print("[-] Could not reconnect to device after reboot.")
        sys.exit(1)

    print("[+] Connected! Querying upgraded image slots...")
    final_states = await client_verify.request(ImageStatesRead())
    print("============================================================")
    print("             POST-UPGRADE IMAGE SLOT STATUS                 ")
    print("============================================================")
    upgraded = False
    for img in final_states.images:
        status = []
        if img.active: status.append("ACTIVE")
        if img.confirmed: status.append("CONFIRMED")
        if img.pending: status.append("PENDING")
        if img.bootable: status.append("BOOTABLE")
        print(f"  Slot #{img.slot}: v{img.version} | Hash: {img.hash.hex()[:16]}... | [{', '.join(status)}]")
        if img.slot == 0 and img.active:
            print(f"\n[SUCCESS] Device is now running version {img.version} in Slot #0!")
            upgraded = True

    print("============================================================")
    await client_verify.disconnect()
    disconnect_bluez(address)

    if not upgraded:
        print("[-] Upgrade failed or device reverted to previous image.")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="FeedbackNow Over-the-Air (OTA) DFU Automated Test Tool.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "address",
        nargs="?",
        default=DEFAULT_ADDR,
        help="Target device BLE MAC address",
    )
    parser.add_argument(
        "bin_file",
        nargs="?",
        default=DEFAULT_BIN,
        help="Path to signed firmware binary",
    )
    args = parser.parse_args()
    asyncio.run(run_ota(args.address, args.bin_file))


if __name__ == "__main__":
    main()
