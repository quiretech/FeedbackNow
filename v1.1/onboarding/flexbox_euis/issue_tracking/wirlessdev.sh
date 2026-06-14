#!/bin/bash

INPUT="v140_bug_units.csv"
OUTPUT="v140_bug_units_updated.csv"

python3 <<'PY'
import csv
import json
import subprocess

INPUT = "v140_bug_units.csv"
OUTPUT = "v140_bug_units_updated.csv"

rows = []

with open(INPUT, newline='') as f:
    reader = csv.DictReader(f)

    fieldnames = list(reader.fieldnames)

    # ensure column exists
    if "wirelessDeviceId" not in fieldnames:
        fieldnames.append("wirelessDeviceId")

    for row in reader:
        dev_eui = row.get("dev_eui", "").strip()

        if not dev_eui:
            rows.append(row)
            continue

        print(f"Looking up {dev_eui}...")

        try:
            result = subprocess.run(
                [
                    "aws",
                    "iotwireless",
                    "get-wireless-device",
                    "--identifier",
                    dev_eui.lower(),
                    "--identifier-type",
                    "DevEui",
                    "--output",
                    "json",
                ],
                capture_output=True,
                text=True,
                check=True,
            )

            device = json.loads(result.stdout)
            row["wirelessDeviceId"] = device.get("Id", "")

            print(f"  -> {row['wirelessDeviceId']}")

        except subprocess.CalledProcessError as e:
            print(f"  ERROR: {e.stderr.strip()}")
            row["wirelessDeviceId"] = ""

        rows.append(row)

with open(OUTPUT, "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

print(f"\nWrote {OUTPUT}")
PY