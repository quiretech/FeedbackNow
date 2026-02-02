"""
Minimal CLI for testing PN5180: EEPROM/version sanity, inventory, read/write block.
Run on Raspberry Pi with PN5180 connected.

  python -m app.main                    # init + version + inventory
  python -m app.main --read-block 5     # inventory then read block 5
  python -m app.main --write-block 5 AABBCCDD   # inventory then write 4 bytes hex to block 5
"""
import argparse
import sys
from pathlib import Path

# Project root (rwPi/) for imports
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app.config import get_config, get_spi_path
from nfc.pn5180 import PN5180, strerror, PN5180_OK, PN5180_ERR_GPIO


def main() -> None:
    parser = argparse.ArgumentParser(description="PN5180 ISO15693 test CLI")
    parser.add_argument("--eeprom-only", action="store_true",
                        help="Only init + EEPROM version read (verify SPI/GPIO like lpgio_eeprom.py)")
    parser.add_argument("--debug", action="store_true",
                        help="Print inventory RX_STATUS and buffer per slot to stderr")
    parser.add_argument("--read-block", type=int, metavar="N", help="After inventory, read block N (0-27)")
    parser.add_argument("--write-block", type=str, metavar="N HEX", nargs=2,
                        help="After inventory, write 4-byte HEX to block N (e.g. 5 AABBCCDD)")
    args = parser.parse_args()

    cfg = get_config()
    spi_path = get_spi_path(cfg)
    dev = PN5180(
        spi_path=spi_path,
        gpio_nss=cfg["gpio_nss"],
        gpio_rst=cfg["gpio_rst"],
        gpio_busy=cfg["gpio_busy"],
        gpio_irq=cfg.get("gpio_irq"),
        timeout_ms=cfg["timeout_ms"],
        spi_speed_hz=cfg.get("spi_speed_hz", 1_000_000),
    )

    print("PN5180 init...")
    err = dev.init()
    if err != PN5180_OK:
        print("Init failed:", strerror(err))
        if err == PN5180_ERR_GPIO:
            print("Hint: GPIO often needs root. Try: sudo python -m app.main")
        dev.close()
        sys.exit(1)
    print("Init OK.")

    # EEPROM / version sanity check
    print("EEPROM version check...")
    err, info = dev.get_version()
    if err != PN5180_OK:
        print("Version read failed:", strerror(err))
        dev.close()
        sys.exit(1)
    print("  Product:", (info["product_version"] >> 8) & 0xFF, ".", info["product_version"] & 0xFF)
    print("  Firmware:", (info["firmware_version"] >> 8) & 0xFF, ".", info["firmware_version"] & 0xFF)
    print("  EEPROM:", (info["eeprom_version"] >> 8) & 0xFF, ".", info["eeprom_version"] & 0xFF)
    print("Version check OK.")

    if args.eeprom_only:
        prod = dev.read_eeprom(0x10, 2)[1]
        fw = dev.read_eeprom(0x12, 2)[1]
        eep = dev.read_eeprom(0x14, 2)[1]
        print("\nRaw bytes:")
        print("Product :", prod.hex())
        print("Firmware:", fw.hex())
        print("EEPROM  :", eep.hex())
        print("\nPN5180 SPI + GPIO communication VERIFIED")
        dev.close()
        return

    # Inventory
    print("Inventory (place ISO15693 tag)...")
    err, uid = dev.get_inventory(verbose=args.debug)
    if err != PN5180_OK:
        print("Inventory failed:", strerror(err))
        dev.close()
        sys.exit(1)
    uid_hex = uid.hex().upper()
    print("  UID:", uid_hex)

    # Optional read block
    if args.read_block is not None:
        n = args.read_block
        if n < 0 or n > 27:
            print("Block must be 0-27")
            dev.close()
            sys.exit(1)
        err, data = dev.read_block(uid, n)
        if err != PN5180_OK:
            print("Read block", n, "failed:", strerror(err))
            dev.close()
            sys.exit(1)
        print("  Block", n, ":", data.hex().upper())

    # Optional write block
    if args.write_block is not None:
        n = int(args.write_block[0])
        hex_str = args.write_block[1].replace(" ", "")
        if n < 0 or n > 27:
            print("Block must be 0-27")
            dev.close()
            sys.exit(1)
        if len(hex_str) != 8:
            print("Data must be 4 bytes = 8 hex chars (e.g. AABBCCDD)")
            dev.close()
            sys.exit(1)
        try:
            data = bytes.fromhex(hex_str)
        except ValueError:
            print("Invalid hex:", args.write_block[1])
            dev.close()
            sys.exit(1)
        err = dev.write_block(uid, n, data)
        if err != PN5180_OK:
            print("Write block", n, "failed:", strerror(err))
            dev.close()
            sys.exit(1)
        print("  Block", n, "written:", data.hex().upper())

    dev.close()
    print("Done.")


if __name__ == "__main__":
    main()
