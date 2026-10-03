#!/usr/bin/env python3
"""
Aggregate Box Order CSV files from the Single/ directory into a unified CSV.

Features:
- Places the 'ID' column as the first column in the CSV.
- Automatically discovers all CSV files in Single/ (ignoring lock / hidden files).
- Deduplicates rows by ID/OrderID, updating records cleanly.
- Sorts rows by ID (numerically where possible).
- Supports single-run aggregation or continuous watch mode (--watch).
"""

from __future__ import annotations

import argparse
import csv
import logging
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_SINGLE_DIR = SCRIPT_DIR / "Single"
DEFAULT_OUTPUT_CSV = SCRIPT_DIR / "all_box_orders.csv"

logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
logger = logging.getLogger("aggregate_orders")


def is_valid_csv_file(path: Path) -> bool:
    """Return True if path is a regular CSV file and not a hidden or lock file."""
    return (
        path.is_file()
        and path.suffix.lower() == ".csv"
        and not path.name.startswith(".")
        and not path.name.startswith("~")
        and not path.name.endswith("#")
    )


def extract_sort_key(id_val: str) -> tuple[int, int | str]:
    """Helper to sort rows numerically by ID when possible, falling back to string sort."""
    id_str = (id_val or "").strip()
    try:
        return (0, int(id_str))
    except ValueError:
        return (1, id_str.lower())


def collect_box_orders(
    single_dir: Path,
) -> tuple[list[str], list[dict[str, str]]]:
    """
    Reads all valid CSV files from single_dir.

    Returns:
        header: List of column names with 'ID' as the first element.
        rows: List of row dictionaries, deduplicated and sorted by ID.
    """
    csv_files = sorted(
        [p for p in single_dir.iterdir() if is_valid_csv_file(p)],
        key=lambda p: p.stat().st_mtime,
    )

    all_keys_ordered: list[str] = []
    records_by_key: dict[str, dict[str, str]] = {}

    for file_path in csv_files:
        try:
            with file_path.open("r", encoding="utf-8-sig") as f:
                reader = csv.DictReader(f)
                if not reader.fieldnames:
                    continue

                for field in reader.fieldnames:
                    if field and field not in all_keys_ordered:
                        all_keys_ordered.append(field)

                for row in reader:
                    if not row or not any(row.values()):
                        continue

                    row_id = (row.get("ID") or "").strip()
                    order_id = (row.get("OrderID") or "").strip()
                    dedup_key = row_id or order_id or file_path.stem

                    records_by_key[dedup_key] = {
                        k: (v or "").strip() for k, v in row.items()
                    }
        except Exception as err:
            logger.warning("Error reading '%s': %s", file_path.name, err)

    # Place 'ID' as the very first column
    id_field_name = next(
        (f for f in all_keys_ordered if f.strip().lower() == "id"), "ID"
    )
    final_header: list[str] = [id_field_name]

    for field in all_keys_ordered:
        if field != id_field_name and field not in final_header:
            final_header.append(field)

    # Sort rows by ID
    sorted_rows = sorted(
        records_by_key.values(),
        key=lambda r: extract_sort_key(r.get(id_field_name, "")),
    )

    return final_header, sorted_rows


def aggregate_box_orders(
    single_dir: Path = DEFAULT_SINGLE_DIR,
    output_csv: Path = DEFAULT_OUTPUT_CSV,
) -> int:
    """
    Aggregates all CSVs from single_dir and writes to output_csv with 'ID' as first column.

    Returns:
        Number of records written.
    """
    if not single_dir.exists():
        logger.error("Single directory does not exist: %s", single_dir)
        return 0

    header, rows = collect_box_orders(single_dir)

    if not header or not rows:
        logger.info("No records found in %s.", single_dir)
        return 0

    output_csv.parent.mkdir(parents=True, exist_ok=True)
    temp_output = output_csv.with_suffix(".tmp")

    with temp_output.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=header, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)

    temp_output.replace(output_csv)
    logger.info(
        "Successfully aggregated %d order(s) into: %s",
        len(rows),
        output_csv,
    )
    return len(rows)


def watch_directory(
    single_dir: Path = DEFAULT_SINGLE_DIR,
    output_csv: Path = DEFAULT_OUTPUT_CSV,
    interval_seconds: float = 2.0,
) -> None:
    """Watch single_dir and automatically re-aggregate whenever files change."""
    logger.info(
        "Watching %s for new/modified CSV files (Ctrl+C to exit)...", single_dir
    )
    last_mtimes: dict[str, float] = {}

    while True:
        try:
            current_files = [
                p for p in single_dir.iterdir() if is_valid_csv_file(p)
            ]
            current_mtimes = {
                p.name: p.stat().st_mtime for p in current_files
            }

            if current_mtimes != last_mtimes:
                logger.info("Change detected in %s. Updating...", single_dir.name)
                aggregate_box_orders(single_dir, output_csv)
                last_mtimes = current_mtimes

            time.sleep(interval_seconds)
        except KeyboardInterrupt:
            logger.info("Watch mode stopped.")
            break
        except Exception as err:
            logger.error("Error during watch: %s", err)
            time.sleep(interval_seconds)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Aggregate individual Box Order CSV files into a single master CSV."
    )
    parser.add_argument(
        "--single-dir",
        type=Path,
        default=DEFAULT_SINGLE_DIR,
        help="Path to Single/ directory (default: %(default)s)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT_CSV,
        help="Path to output CSV file (default: %(default)s)",
    )
    parser.add_argument(
        "--watch",
        action="store_true",
        help="Continuously watch Single/ directory for new or updated files.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.watch:
        watch_directory(args.single_dir, args.output)
        return 0

    count = aggregate_box_orders(args.single_dir, args.output)
    return 0 if count > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
