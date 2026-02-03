"""
CSV export for Flash workflow: append one row per successful Flash.
Columns: timestamp_utc, uid_hex, employee_name, block_number, data_hex.
Creates file with header if missing.
"""
import csv
from datetime import datetime, timezone
from pathlib import Path


CSV_HEADER = ["timestamp_utc", "uid_hex", "employee_name", "block_number", "data_hex"]


def append_flash_row(
    csv_path: str,
    uid_hex: str,
    employee_name: str,
    block_number: int,
    data_hex: str,
) -> None:
    """
    Append one row to the CSV at csv_path.
    Creates the file with header if it does not exist.
    timestamp_utc is set to now (ISO format UTC).
    """
    path = Path(csv_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    file_exists = path.exists()
    timestamp_utc = datetime.now(timezone.utc).isoformat()
    row = [timestamp_utc, uid_hex, employee_name, str(block_number), data_hex]
    with open(path, "a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        if not file_exists:
            writer.writerow(CSV_HEADER)
        writer.writerow(row)
