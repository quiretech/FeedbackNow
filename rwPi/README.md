# rwPi — PN5180 ISO15693 Flash app on Raspberry Pi

Raspberry Pi app for PN5180 NFC reader: read/write ISO15693 (e.g. SLIX) tags and a **Flash** web UI to write 4-byte data to a block and append rows to a CSV for asset tracking.

## Hardware

- **Raspberry Pi** (with SPI and GPIO)
- **PN5180** module (ISO15693)
- Wiring: see [Pinout](#pinout)

Enable SPI: `sudo raspi-config` → Interface Options → SPI → Enable.

## Pinout (BCM)

| PN5180 | RPi GPIO | 40-pin header |
|--------|----------|---------------|
| VCC    | 3.3 V    | Pin 1 or 17   |
| GND    | GND      | Pin 6, 9, 14, … |
| SCK    | GPIO 11  | Pin 23 (SPI0 SCLK) |
| MOSI   | GPIO 10  | Pin 19 (SPI0 MOSI) |
| MISO   | GPIO 9   | Pin 21 (SPI0 MISO) |
| NSS/CS | GPIO 22  | Pin 15 (manual CS) |
| RST    | GPIO 17  | Pin 11         |
| BUSY   | GPIO 27  | Pin 13         |

NSS must be software-controlled (do not use kernel CE0). Override pins in `config/default.yaml` or via `RWPI_GPIO_*` env vars.

## Install

From project root (rwPi/):

```bash
pip install -r requirements.txt
```

## CLI (testing)

```bash
sudo python -m app.main                    # init + version + inventory
sudo python -m app.main --eeprom-only      # EEPROM/version only (verify SPI)
sudo python -m app.main --read-block 5    # inventory then read block 5
sudo python -m app.main --write-block 5 AABBCCDD   # write 4 bytes to block 5
```

## Flash web app

1. Start the server (on the RPi):

   ```bash
   sudo python -m app.main --web
   ```
   Or: `sudo python -m app.flask_app` (optional: `--port 5000` `--host 0.0.0.0`).

2. Open in a browser:
   - **Local (7" screen on RPi):** `http://127.0.0.1:5000` or `http://localhost:5000`
   - **From PC over Ethernet:** Connect RPi to PC via Ethernet, find RPi IP (e.g. `ip addr`), then open `http://<RPi-IP>:5000`

3. **Workflow:** Place tag on reader → paste employee name (for CSV) → enter 4-byte hex (e.g. `AABBCCDD`) → set block (default 5, 0–27) → click **Flash!** → dialog shows UID and success → one row is appended to the CSV. Use **Scan** to read UID only. Use **Download CSV** to get the log file.

## Config

- **config/default.yaml** — optional overrides for SPI bus, GPIO pins, `csv_path`, `timeout_ms`, etc.
- **Environment:** e.g. `RWPI_GPIO_NSS=22`, `RWPI_CSV_PATH=/path/to/log.csv`

## CSV format

After each successful Flash, one row is appended to the CSV (path from config, default `nfc_log.csv`):

- **Header:** `timestamp_utc,uid_hex,employee_name,block_number,data_hex`
- Employee name and UID are for asset tracking only; only the 4-byte hex is written to the tag.

## Project layout

```
rwPi/
├── app/           # Config, CLI (main.py), Flask app (flask_app.py)
├── app/templates/ # Web UI (index.html)
├── config/        # default.yaml
├── export/        # CSV append (csv_export.py)
├── nfc/           # PN5180 driver (pn5180.py), ISO15693 (iso15693.py)
├── requirements.txt
├── README.md
└── zephyr_src/    # Reference Zephyr driver (not used at runtime)
```

```
fbn@fbn
root
```