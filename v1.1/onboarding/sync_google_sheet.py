#!/usr/bin/env python3
"""
Sync local FlexBox EUI registry CSV to a Google Sheet on Google Drive using gspread.

Default target CSV:
  onboarding/flexbox_euis/eui_registry.csv

Usage Examples:
  # Using Google Sheet URL:
  python3 onboarding/sync_google_sheet.py --sheet-url "https://docs.google.com/spreadsheets/d/<SPREADSHEET_ID>/edit"

  # Using Google Sheet Name:
  python3 onboarding/sync_google_sheet.py --sheet-name "FlexBox EUI Registry"

  # Using a Service Account credentials JSON:
  python3 onboarding/sync_google_sheet.py --sheet-url "..." --service-account path/to/service_account.json

  # Using OAuth (opens browser to sign into your personal Google account):
  python3 onboarding/sync_google_sheet.py --sheet-url "..." --oauth

  # Dry run (test connection and preview changes without modifying sheet):
  python3 onboarding/sync_google_sheet.py --sheet-url "..." --dry-run

  # Append only new rows (instead of full replace):
  python3 onboarding/sync_google_sheet.py --sheet-url "..." --mode append
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
from pathlib import Path
from typing import Any, Iterable

_ONBOARDING = Path(__file__).resolve().parent
if str(_ONBOARDING) not in sys.path:
    sys.path.insert(0, str(_ONBOARDING))

try:
    import gspread
except ImportError as e:
    print(
        f"Error: {e}\n\n"
        "Please run:\n"
        f"    {sys.executable} -m pip install gspread google-auth\n"
        "or (if using Debian/Ubuntu system Python):\n"
        f"    {sys.executable} -m pip install --break-system-packages gspread google-auth\n",
        file=sys.stderr,
    )
    sys.exit(1)

DEFAULT_CONFIG_PATH = _ONBOARDING / "google_sheets_config.json"
DEFAULT_CSV_US915 = _ONBOARDING / "flexbox_euis" / "eui_registry.csv"
DEFAULT_CSV_EU868 = _ONBOARDING / "flexbox_euis" / "eui_registry_EU868.csv"
DEFAULT_CSV_PATH = DEFAULT_CSV_US915

DEFAULT_WORKSHEET_US915 = "Device EUI US915"
DEFAULT_WORKSHEET_EU868 = "Device EUI EU868"

PRESETS_REGIONS: dict[str, str] = {
    "lr-us915": "us915",
    "lr": "us915",
    "seeed-us915": "us915",
    "s-us915": "us915",
    "s_us915": "us915",
    "seeed-eu868": "eu868",
    "s-eu868": "eu868",
    "s_eu": "eu868",
}


def load_config(config_path: Path | None = None) -> dict[str, Any]:
    """Load optional configuration file with sheet URL, ID, or credentials path."""
    target_path = config_path or DEFAULT_CONFIG_PATH
    if not target_path.is_file() and not config_path:
        example_path = _ONBOARDING / "google_sheets_config.json.example"
        if example_path.is_file():
            target_path = example_path

    if target_path.is_file():
        try:
            with target_path.open("r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as e:
            print(f"Warning: Failed to load config from {target_path}: {e}", file=sys.stderr)
    return {}


def find_service_account_credentials(explicit_path: str | None = None) -> Path | None:
    """Find service account JSON file from arguments, env, config, or standard paths."""
    candidates: list[Path] = []
    if explicit_path:
        candidates.append(Path(explicit_path).expanduser().resolve())

    env_creds = os.environ.get("GOOGLE_APPLICATION_CREDENTIALS")
    if env_creds:
        candidates.append(Path(env_creds).expanduser().resolve())

    # Look in onboarding directory
    candidates.extend([
        _ONBOARDING / "service_account.json",
        _ONBOARDING / "credentials.json",
        Path.home() / ".config" / "gspread" / "service_account.json",
    ])

    for p in candidates:
        if p.is_file():
            return p
    return None


def get_gspread_client(
    *,
    use_oauth: bool = False,
    service_account_path: str | None = None,
    credentials_dir: Path | None = None,
) -> tuple[gspread.Client, str]:
    """Authenticate with Google Sheets via Service Account or OAuth."""
    if use_oauth:
        # OAuth interactive user flow
        creds_dir = credentials_dir or (Path.home() / ".config" / "gspread")
        creds_dir.mkdir(parents=True, exist_ok=True)
        creds_file = creds_dir / "credentials.json"
        auth_user_file = creds_dir / "authorized_user.json"

        # Check for local OAuth client file in onboarding directory
        for local_candidate in [_ONBOARDING / "oauth_credentials.json", _ONBOARDING / "credentials.json"]:
            if local_candidate.is_file():
                try:
                    with local_candidate.open("r", encoding="utf-8") as f:
                        data = json.load(f)
                    if "installed" in data or "web" in data:
                        creds_file = local_candidate
                        break
                except Exception:
                    pass

        print(f"Authenticating via Google OAuth (client file: {creds_file})...")
        if not auth_user_file.exists() and not creds_file.exists():
            print(
                "\nNote: For OAuth, please place your OAuth client ID credentials in:\n"
                f"  {creds_file}\n"
                "or use a Service Account JSON with --service-account <path>.\n",
                file=sys.stderr,
            )
        client = gspread.oauth(
            credentials_filename=creds_file,
            authorized_user_filename=auth_user_file,
        )
        return client, "OAuth User"

    # Service Account authentication
    sa_path = find_service_account_credentials(service_account_path)
    if sa_path and sa_path.is_file():
        print(f"Authenticating with Service Account: {sa_path}")
        with sa_path.open("r", encoding="utf-8") as f:
            sa_data = json.load(f)
            client_email = sa_data.get("client_email", "service account")
        client = gspread.service_account(filename=str(sa_path))
        return client, client_email

    # If neither found, provide clear guidance
    raise FileNotFoundError(
        "Google credentials not found!\n\n"
        "To set up authentication:\n"
        "  Option A (Recommended for automated scripts - Service Account):\n"
        "    1. Go to Google Cloud Console (https://console.cloud.google.com/)\n"
        "    2. Enable Google Sheets API & Google Drive API\n"
        "    3. Create a Service Account, generate a JSON Key, and save it as:\n"
        f"       {_ONBOARDING / 'service_account.json'}\n"
        "    4. Share your Google Sheet with the Service Account email (Editor permissions).\n\n"
        "  Option B (OAuth user login):\n"
        "    Run with --oauth flag (requires OAuth Client ID credentials in ~/.config/gspread/credentials.json).\n"
    )


def read_csv_rows(csv_path: Path) -> list[list[str]]:
    """Read CSV file and return all rows as list of lists."""
    if not csv_path.is_file():
        raise FileNotFoundError(f"CSV file not found: {csv_path}")

    with csv_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        rows = [list(row) for row in reader]

    if not rows:
        raise ValueError(f"CSV file is empty: {csv_path}")

    return rows


def open_spreadsheet(
    gc: gspread.Client,
    *,
    sheet_url: str | None = None,
    sheet_id: str | None = None,
    sheet_name: str | None = None,
) -> gspread.Spreadsheet:
    """Open Google Spreadsheet by URL, ID, or title."""
    if sheet_url:
        return gc.open_by_url(sheet_url)
    if sheet_id:
        return gc.open_by_key(sheet_id)
    if sheet_name:
        return gc.open(sheet_name)
    raise ValueError("Must provide --sheet-url, --sheet-id, or --sheet-name")


def get_worksheet(
    spreadsheet: gspread.Spreadsheet,
    worksheet_identifier: str | None = None,
) -> gspread.Worksheet:
    """Retrieve target worksheet by title or index, defaulting to sheet1."""
    if not worksheet_identifier:
        return spreadsheet.sheet1

    # Check if index number
    if worksheet_identifier.isdigit():
        idx = int(worksheet_identifier)
        return spreadsheet.get_worksheet(idx)

    try:
        return spreadsheet.worksheet(worksheet_identifier)
    except gspread.exceptions.WorksheetNotFound:
        print(f"Worksheet '{worksheet_identifier}' not found. Available sheets:")
        for ws in spreadsheet.worksheets():
            print(f" - {ws.title}")
        raise


def sync_registry_to_sheet(
    *,
    csv_path: Path,
    sheet_url: str | None = None,
    sheet_id: str | None = None,
    sheet_name: str | None = None,
    worksheet_id: str | None = None,
    service_account_path: str | None = None,
    use_oauth: bool = False,
    mode: str = "replace",
    freeze_header: bool = True,
    dry_run: bool = False,
) -> None:
    """Sync CSV to Google Sheet."""
    print(f"Reading CSV: {csv_path}")
    rows = read_csv_rows(csv_path)
    total_csv_rows = len(rows)
    headers = rows[0]
    data_rows = rows[1:]
    print(f"Loaded {total_csv_rows} rows (1 header + {len(data_rows)} data rows, {len(headers)} columns).")

    if dry_run:
        print("\n--- DRY RUN MODE ---")
        print(f"Target CSV: {csv_path}")
        print(f"Header columns: {headers}")
        if data_rows:
            print(f"First row sample: {data_rows[0]}")
            print(f"Last row sample:  {data_rows[-1]}")
        print(f"Total rows to sync: {total_csv_rows}")
        print(f"Sync mode: {mode}")

    # Authenticate
    client, auth_identity = get_gspread_client(
        use_oauth=use_oauth,
        service_account_path=service_account_path,
    )
    print(f"Authenticated as: {auth_identity}")

    try:
        spreadsheet = open_spreadsheet(
            client,
            sheet_url=sheet_url,
            sheet_id=sheet_id,
            sheet_name=sheet_name,
        )
    except gspread.exceptions.SpreadsheetNotFound:
        target_desc = sheet_url or sheet_id or sheet_name
        raise RuntimeError(
            f"Spreadsheet not found: {target_desc}\n"
            f"If using a Service Account ({auth_identity}), make sure you have shared "
            f"the Google Sheet with this email address with 'Editor' permissions."
        )

    worksheet = get_worksheet(spreadsheet, worksheet_id)
    print(f"Connected to Spreadsheet: '{spreadsheet.title}' (Worksheet: '{worksheet.title}')")

    if dry_run:
        existing_rows = worksheet.get_all_values()
        print(f"Target worksheet currently contains {len(existing_rows)} rows.")
        print("Dry run complete: No changes made to Google Sheet.")
        return

    if mode == "replace":
        print(f"Clearing worksheet '{worksheet.title}'...")
        worksheet.clear()

        # Ensure worksheet dimensions are large enough
        required_rows = max(total_csv_rows + 10, 50)
        required_cols = max(len(headers) + 2, 15)
        if worksheet.row_count < required_rows or worksheet.col_count < required_cols:
            worksheet.resize(
                rows=max(worksheet.row_count, required_rows),
                cols=max(worksheet.col_count, required_cols),
            )

        print(f"Uploading {total_csv_rows} rows to '{worksheet.title}' (RAW mode to preserve hex EUIs/keys)...")
        # raw=True ensures hex strings like '464C58C120FF0B8E' are not parsed as scientific notation
        worksheet.update(rows, raw=True)

        if freeze_header:
            try:
                worksheet.freeze(rows=1)
            except Exception as e:
                print(f"Notice: Could not freeze header row: {e}")

        print(f"Successfully synced {total_csv_rows} rows to '{spreadsheet.title}' [{worksheet.title}].")

    elif mode == "append":
        existing_values = worksheet.get_all_values()
        if not existing_values:
            print("Worksheet is currently empty. Writing full CSV including header...")
            worksheet.update(rows, raw=True)
            if freeze_header:
                worksheet.freeze(rows=1)
            print(f"Successfully wrote {total_csv_rows} rows.")
            return

        # Determine key column (asset_id or dev_eui)
        existing_headers = existing_values[0]
        key_col_idx = 0
        if "asset_id" in existing_headers:
            key_col_idx = existing_headers.index("asset_id")
        elif "dev_eui" in existing_headers:
            key_col_idx = existing_headers.index("dev_eui")

        existing_keys = {
            r[key_col_idx].strip() for r in existing_values[1:] if len(r) > key_col_idx and r[key_col_idx].strip()
        }

        # Find rows in CSV that don't exist yet
        csv_key_idx = 0
        if "asset_id" in headers:
            csv_key_idx = headers.index("asset_id")
        elif "dev_eui" in headers:
            csv_key_idx = headers.index("dev_eui")

        new_rows = [
            r for r in data_rows
            if len(r) > csv_key_idx and r[csv_key_idx].strip() and r[csv_key_idx].strip() not in existing_keys
        ]

        if not new_rows:
            print("No new rows found in CSV. Google Sheet is already up to date.")
            return

        print(f"Appending {len(new_rows)} new rows to '{worksheet.title}'...")
        worksheet.append_rows(new_rows, raw=True)
        print(f"Successfully appended {len(new_rows)} rows.")

    elif mode == "update":
        print("Update mode: performing full raw update to ensure all metadata is synchronized...")
        worksheet.clear()
        required_rows = max(total_csv_rows + 10, 50)
        required_cols = max(len(headers) + 2, 15)
        if worksheet.row_count < required_rows or worksheet.col_count < required_cols:
            worksheet.resize(
                rows=max(worksheet.row_count, required_rows),
                cols=max(worksheet.col_count, required_cols),
            )
        worksheet.update(rows, raw=True)
        if freeze_header:
            worksheet.freeze(rows=1)
        print(f"Successfully updated {total_csv_rows} rows.")


def sync_via_apps_script_webhook(
    webhook_url: str,
    csv_path: Path,
    worksheet: str | None = None,
    dry_run: bool = False,
) -> None:
    """Sync CSV directly to Google Sheet via Google Apps Script Web App (No Google Cloud required)."""
    import requests

    rows = read_csv_rows(csv_path)
    total_csv_rows = len(rows)
    print(f"Reading CSV: {csv_path}")
    print(f"Loaded {total_csv_rows} rows from {csv_path.name}")

    if dry_run:
        print("\n--- DRY RUN (WEBHOOK MODE) ---")
        print(f"Target Webhook: {webhook_url}")
        print(f"Worksheet tab: {worksheet or '(default first sheet)'}")
        print(f"Total rows to send: {total_csv_rows}")
        print("Dry run complete: No data sent to Google Sheet.")
        return

    with csv_path.open("r", encoding="utf-8") as f:
        csv_text = f.read()

    payload = {
        "csv": csv_text,
        "worksheet": worksheet or "",
    }

    print(f"Sending {total_csv_rows} rows to Google Sheet Webhook...")
    try:
        resp = requests.post(webhook_url, json=payload, timeout=60, allow_redirects=True)
        if resp.status_code == 200:
            print(f"Google Sheet response: {resp.text.strip()}")
            print("Successfully updated Google Sheet!")
        else:
            print(f"Error ({resp.status_code}): {resp.text}", file=sys.stderr)
            resp.raise_for_status()
    except Exception as e:
        raise RuntimeError(f"Failed to post to Google Apps Script Webhook: {e}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Sync local FlexBox EUI registry CSV to a Google Sheet on Google Drive.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    # Target Google Sheet
    target_group = parser.add_argument_group("Google Sheet Target (choose at least one)")
    target_group.add_argument("--sheet-url", help="Full URL of the Google Sheet")
    target_group.add_argument("--sheet-id", help="Spreadsheet ID (from URL /d/<ID>/edit)")
    target_group.add_argument("--sheet-name", help="Title of Google Sheet in Drive")
    target_group.add_argument(
        "--worksheet",
        help="Worksheet / tab title or 0-based index (default: first worksheet)",
    )
    target_group.add_argument(
        "--webhook-url",
        help="Google Apps Script Web App URL (no Google Cloud setup needed)",
    )

    # Input CSV & Region Selection
    csv_group = parser.add_argument_group("CSV & Region Options")
    csv_group.add_argument(
        "--csv",
        type=Path,
        default=None,
        help="Path to specific registry CSV file (overrides region defaults)",
    )
    csv_group.add_argument(
        "--region",
        choices=["us915", "eu868"],
        help="LoRaWAN region: 'us915' (eui_registry.csv) or 'eu868' (eui_registry_EU868.csv)",
    )
    csv_group.add_argument(
        "--preset",
        help="Hardware preset (lr-us915, seeed-eu868, etc.) to pick region and CSV",
    )
    csv_group.add_argument(
        "-eu",
        "--eu",
        action="store_true",
        help="Sync EU868 registry (eui_registry_EU868.csv) to 'Device EUI EU868' tab",
    )
    csv_group.add_argument(
        "-us",
        "--us",
        action="store_true",
        help="Sync US915 registry (eui_registry.csv) to 'Device EUI US915' tab",
    )
    csv_group.add_argument(
        "-all",
        "--all",
        "--all-registries",
        dest="sync_all",
        action="store_true",
        help="Sync BOTH US915 and EU868 registries to their respective tabs in the sheet",
    )

    # Auth
    auth_group = parser.add_argument_group("Authentication")
    auth_group.add_argument(
        "--service-account",
        "--creds",
        dest="service_account",
        help="Path to Google Cloud Service Account JSON credentials",
    )
    auth_group.add_argument(
        "--oauth",
        action="store_true",
        help="Use browser OAuth user authentication instead of service account",
    )

    # Options
    opt_group = parser.add_argument_group("Sync Options")
    opt_group.add_argument(
        "--mode",
        choices=["replace", "append", "update"],
        default="replace",
        help="Sync mode: 'replace' clears and writes full CSV (default), 'append' adds only new rows, 'update' refreshes existing",
    )
    opt_group.add_argument(
        "--no-freeze-header",
        action="store_true",
        help="Do not freeze header row (row 1)",
    )
    opt_group.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate CSV and connect to sheet without modifying data",
    )
    opt_group.add_argument(
        "--config",
        type=Path,
        help="Path to optional JSON config file with default sheet ID/URL/credentials",
    )

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    # Merge configuration file if present
    cfg = load_config(args.config)
    webhook_url = args.webhook_url or cfg.get("webhook_url")
    sheet_url = args.sheet_url or cfg.get("sheet_url")
    sheet_id = args.sheet_id or cfg.get("sheet_id")
    sheet_name = args.sheet_name or cfg.get("sheet_name")
    service_account = args.service_account or cfg.get("service_account_path")

    # Define helper to resolve (csv_path, worksheet_name) for a given region
    def resolve_target(region_key: str) -> tuple[Path, str]:
        if region_key == "eu868":
            target_csv = args.csv or DEFAULT_CSV_EU868
            target_ws = args.worksheet or cfg.get("worksheet_eu868") or DEFAULT_WORKSHEET_EU868
        else:
            target_csv = args.csv or DEFAULT_CSV_US915
            target_ws = args.worksheet or cfg.get("worksheet_us915") or cfg.get("worksheet") or DEFAULT_WORKSHEET_US915
        return target_csv.resolve(), target_ws

    # Determine tasks to run: list of (csv_path, worksheet_name)
    tasks: list[tuple[Path, str]] = []

    if args.sync_all:
        tasks.append(resolve_target("us915"))
        tasks.append(resolve_target("eu868"))
    else:
        # Determine region
        if args.eu:
            chosen_region = "eu868"
        elif args.us:
            chosen_region = "us915"
        elif args.region:
            chosen_region = args.region
        elif args.preset:
            chosen_region = PRESETS_REGIONS.get(args.preset.strip().lower(), "us915")
        elif args.csv and "eu868" in args.csv.name.lower():
            chosen_region = "eu868"
        else:
            chosen_region = "us915"

        tasks.append(resolve_target(chosen_region))

    # Execute sync for each task
    for csv_path, worksheet_id in tasks:
        if not csv_path.is_file():
            print(f"Error: CSV file not found at {csv_path}", file=sys.stderr)
            return 1

        print(f"\n==================================================")
        print(f"Syncing: {csv_path.name}  ->  Worksheet Tab: '{worksheet_id}'")
        print(f"==================================================")

        # Option 1: Webhook URL mode (Zero Google Cloud needed)
        if webhook_url:
            try:
                sync_via_apps_script_webhook(
                    webhook_url=webhook_url,
                    csv_path=csv_path,
                    worksheet=worksheet_id,
                    dry_run=args.dry_run,
                )
            except Exception as e:
                print(f"\nWebhook sync failed: {e}", file=sys.stderr)
                return 1

        # Option 2: Direct gspread API mode (requires service account or OAuth)
        else:
            if not sheet_url and not sheet_id and not sheet_name:
                parser.error(
                    "Must specify target Google Sheet via --webhook-url, --sheet-url, --sheet-id, or --sheet-name "
                    f"(or configure in {DEFAULT_CONFIG_PATH})"
                )
            try:
                sync_registry_to_sheet(
                    csv_path=csv_path,
                    sheet_url=sheet_url,
                    sheet_id=sheet_id,
                    sheet_name=sheet_name,
                    worksheet_id=worksheet_id,
                    service_account_path=service_account,
                    use_oauth=args.oauth,
                    mode=args.mode,
                    freeze_header=not args.no_freeze_header,
                    dry_run=args.dry_run,
                )
            except Exception as e:
                print(f"\nSync failed: {e}", file=sys.stderr)
                return 1

    print("\nAll requested worksheet updates completed successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
