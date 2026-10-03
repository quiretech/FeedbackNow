#!/usr/bin/env python3
"""
Generate a Job Order Form PDF from a row in all_box_orders.csv.

Usage:
  python3 generate_job_order.py --id 33
  python3 generate_job_order.py --id 24
  python3 generate_job_order.py --all

Output:
  PDF saved in v1.1/onboarding/Box_Orders/Job_Orders/ with filename format:
  JobOrder_{ID}_{ClientName}_{UnitType}_{DeliveryDate}_Qty{TotalQuantity}.pdf
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from datetime import datetime, timezone
import glob
from pathlib import Path

# Ensure user site-packages across Python versions are available (e.g. ncs python3.12 vs system 3.14)
for site_pkg in glob.glob(str(Path.home() / ".local" / "lib" / "python*" / "site-packages")):
    if site_pkg not in sys.path:
        sys.path.insert(0, site_pkg)

try:
    from reportlab.lib import colors
    from reportlab.lib.pagesizes import letter
    from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
    from reportlab.platypus import (
        HRFlowable,
        Paragraph,
        SimpleDocTemplate,
        Spacer,
        Table,
        TableStyle,
    )
except ImportError:
    print(
        "ERROR: ReportLab is required to generate PDF order forms.\n"
        "Please install it with: python3 -m pip install reportlab",
        file=sys.stderr,
    )
    sys.exit(1)

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_CSV_PATH = SCRIPT_DIR / "all_box_orders.csv"
DEFAULT_OUTPUT_DIR = SCRIPT_DIR / "Job_Orders"


def sanitize_filename_token(val: str, fallback: str = "Unknown") -> str:
    """Sanitize a string for safe inclusion in filenames."""
    s = str(val or "").strip()
    if not s:
        return fallback
    # Replace whitespace and commas with underscores
    s = re.sub(r"[\s,]+", "_", s)
    # Remove any characters that aren't alphanumeric, underscore, dot, plus, or dash
    s = re.sub(r"[^\w.+-]", "", s)
    return s.strip("._-") or fallback


def format_date_mm_dd_yyyy(date_str: str) -> str:
    """Converts a date string (YYYY-MM-DD or other standard formats) to MM-DD-YYYY."""
    s = str(date_str or "").strip()
    if not s:
        return "-"
    for fmt in ("%Y-%m-%d", "%Y/%m/%d", "%m-%d-%Y", "%m/%d/%Y"):
        try:
            return datetime.strptime(s, fmt).strftime("%m-%d-%Y")
        except ValueError:
            pass
    m = re.match(r"^(\d{4})[/-](\d{1,2})[/-](\d{1,2})$", s)
    if m:
        y, mth, d = m.groups()
        return f"{int(mth):02d}-{int(d):02d}-{y}"
    return s


def format_button_layout_2lines(layout_str: str) -> str:
    """
    Formats ButtonLayout across 2 lines with the same font for both the button code
    on line 1 and the configuration description on line 2.
    """
    s = str(layout_str or "").strip()
    if not s:
        return "—"
    if " - " in s:
        code, desc = s.split(" - ", 1)
        return f"<b>{code.strip()}</b><br/><b>{desc.strip()}</b>"
    elif "-" in s:
        code, desc = s.split("-", 1)
        return f"<b>{code.strip()}</b><br/><b>{desc.strip()}</b>"
    return f"<b>{s}</b>"


def format_button_layout_spec(layout_str: str) -> str:
    """Formats ButtonLayout for the specifications table in 2 clear lines with matching font."""
    s = str(layout_str or "").strip()
    if not s:
        return "—"
    if " - " in s:
        code, desc = s.split(" - ", 1)
        return f"<b>{code.strip()}</b><br/><b>{desc.strip()}</b>"
    elif "-" in s:
        code, desc = s.split("-", 1)
        return f"<b>{code.strip()}</b><br/><b>{desc.strip()}</b>"
    return f"<b>{s}</b>"


def format_lora_region(val: str) -> str:
    """
    Converts raw installation frequency or region string to standard LoRa regional convention
    (e.g., US -> US915, EU -> EU868, AU -> AU915, AS -> AS923).
    """
    s = str(val or "").strip()
    if not s:
        return ""

    norm = re.sub(r"[\s_-]+", "", s).upper()

    mapping = {
        # North America (US915)
        "US": "US915",
        "USA": "US915",
        "US915": "US915",
        "915": "US915",
        # Europe (EU868)
        "EU": "EU868",
        "EUR": "EU868",
        "EUROPE": "EU868",
        "EU868": "EU868",
        "868": "EU868",
        # Australia (AU915)
        "AU": "AU915",
        "AUSTRALIA": "AU915",
        "AU915": "AU915",
        # Asia (AS923)
        "AS": "AS923",
        "ASIA": "AS923",
        "AS923": "AS923",
        "923": "AS923",
        # India (IN865)
        "IN": "IN865",
        "INDIA": "IN865",
        "IN865": "IN865",
        "865": "IN865",
        # Korea (KR920)
        "KR": "KR920",
        "KOREA": "KR920",
        "KR920": "KR920",
        "920": "KR920",
        # Russia (RU864)
        "RU": "RU864",
        "RUSSIA": "RU864",
        "RU864": "RU864",
        "864": "RU864",
    }

    if norm in mapping:
        return mapping[norm]

    # Standard convention pattern (e.g., US915, EU868, etc.)
    if re.fullmatch(r"[A-Z]{2,4}\d{3}", norm):
        return norm

    return s.upper()


def build_job_order_filename(row: dict[str, str]) -> str:
    """
    Constructs PDF file name containing ID number, ClientName, UnitType,
    DeliveryDate (MM-DD-YYYY), and TotalQuantity fields.
    Format: JobOrder_{ID}_{ClientName}_{UnitType}_{DeliveryDate}_Qty{TotalQuantity}.pdf
    """
    order_id = sanitize_filename_token(row.get("ID", ""), fallback="0")
    client = sanitize_filename_token(row.get("ClientName", ""), fallback="Client")
    unit_type = sanitize_filename_token(row.get("UnitType", ""), fallback="Unit")
    raw_date = row.get("DeliveryDate", "")
    date_formatted = format_date_mm_dd_yyyy(raw_date)
    delivery = sanitize_filename_token(date_formatted, fallback="Undated")
    qty = sanitize_filename_token(row.get("TotalQuantity", ""), fallback="1")

    return f"JobOrder_{order_id}_{client}_{unit_type}_{delivery}_Qty{qty}.pdf"


def read_box_order_row(csv_path: Path, target_id: str | int) -> dict[str, str]:
    """Finds and returns the row dictionary matching target_id in csv_path."""
    if not csv_path.exists():
        raise FileNotFoundError(f"Missing master orders CSV: {csv_path}")

    search_id = str(target_id).strip()
    available_ids: list[str] = []

    with csv_path.open("r", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if not row or not any(row.values()):
                continue
            r_id = (row.get("ID") or "").strip()
            r_order_id = (row.get("OrderID") or "").strip()
            if r_id:
                available_ids.append(r_id)
            if r_id == search_id or r_order_id == search_id:
                return {k: (v or "").strip() for k, v in row.items()}

    known = ", ".join(available_ids) if available_ids else "(none)"
    raise ValueError(
        f"Order ID {search_id!r} not found in {csv_path.name}; available IDs: {known}"
    )


def read_all_box_orders(csv_path: Path) -> list[dict[str, str]]:
    """Reads all valid rows from all_box_orders.csv."""
    if not csv_path.exists():
        raise FileNotFoundError(f"Missing master orders CSV: {csv_path}")

    rows: list[dict[str, str]] = []
    with csv_path.open("r", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row and any(row.values()):
                rows.append({k: (v or "").strip() for k, v in row.items()})
    return rows


def generate_pdf_order_form(row: dict[str, str], output_path: Path) -> Path:
    """Generates an executive-style, 1-page Job Order Form PDF from row data."""
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # 0.5 in (36 pt) margins
    doc = SimpleDocTemplate(
        str(output_path),
        pagesize=letter,
        leftMargin=36,
        rightMargin=36,
        topMargin=36,
        bottomMargin=36,
    )

    styles = getSampleStyleSheet()

    # Custom typography
    title_style = ParagraphStyle(
        "HeaderTitle",
        parent=styles["Heading1"],
        fontName="Helvetica-Bold",
        fontSize=18,
        leading=22,
        textColor=colors.HexColor("#0F172A"),
        spaceAfter=2,
    )

    subtitle_style = ParagraphStyle(
        "HeaderSubtitle",
        parent=styles["Normal"],
        fontName="Helvetica",
        fontSize=9,
        leading=12,
        textColor=colors.HexColor("#64748B"),
    )

    kpi_label_style = ParagraphStyle(
        "KpiLabel",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=6.5,
        leading=8,
        textColor=colors.HexColor("#475569"),
        alignment=1,  # Center
    )

    kpi_value_style = ParagraphStyle(
        "KpiValue",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=13.5,
        leading=16.5,
        textColor=colors.HexColor("#0F172A"),
        alignment=1,  # Center
    )

    kpi_btn_style = ParagraphStyle(
        "KpiBtnStyle",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=12,
        leading=14.5,
        textColor=colors.HexColor("#0F172A"),
        alignment=1,  # Center
    )

    epd_badge_style = ParagraphStyle(
        "EpdBadge",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=14,
        leading=17,
        textColor=colors.HexColor("#047857"),  # Emerald green
        alignment=1,  # Center
    )

    non_epd_badge_style = ParagraphStyle(
        "NonEpdBadge",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=13.5,
        leading=16.5,
        textColor=colors.HexColor("#334155"),  # Slate gray
        alignment=1,  # Center
    )

    table_header_style = ParagraphStyle(
        "TableHeader",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=9,
        leading=12,
        textColor=colors.whitesmoke,
    )

    field_label_style = ParagraphStyle(
        "FieldLabel",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=8.5,
        leading=11,
        textColor=colors.HexColor("#1E293B"),
    )

    field_value_style = ParagraphStyle(
        "FieldValue",
        parent=styles["Normal"],
        fontName="Helvetica",
        fontSize=8.5,
        leading=11,
        textColor=colors.HexColor("#0F172A"),
    )

    section_title_style = ParagraphStyle(
        "SectionTitle",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=10,
        leading=13,
        textColor=colors.HexColor("#1E293B"),
        spaceBefore=6,
        spaceAfter=4,
    )

    checklist_item_style = ParagraphStyle(
        "ChecklistItem",
        parent=styles["Normal"],
        fontName="Helvetica",
        fontSize=8,
        leading=11,
        textColor=colors.HexColor("#334155"),
    )

    elements = []

    # 1. Header Banner
    header_data = [
        [
            Paragraph("FEEDBACKNOW — PRODUCTION JOB ORDER", title_style),
            Paragraph(
                f"Generated: {datetime.now(timezone.utc).strftime('%m-%d-%Y %H:%M UTC')}<br/>"
                f"System Form Ref: <b>FBN-PROV-{row.get('ID', '0')}</b>",
                ParagraphStyle("HeaderMeta", parent=subtitle_style, alignment=2),
            ),
        ]
    ]
    header_table = Table(header_data, colWidths=[350, 190])
    header_table.setStyle(
        TableStyle(
            [
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
                ("TOPPADDING", (0, 0), (-1, -1), 0),
                ("LEFTPADDING", (0, 0), (-1, -1), 0),
                ("RIGHTPADDING", (0, 0), (-1, -1), 0),
            ]
        )
    )
    elements.append(header_table)
    elements.append(Spacer(1, 6))
    elements.append(
        HRFlowable(
            width="100%",
            thickness=1.5,
            color=colors.HexColor("#0284C7"),
            spaceBefore=0,
            spaceAfter=6,
        )
    )

    # 2. Key Highlights / KPI Cards Grid with ButtonLayout and EPD/Non-EPD Box
    unit_type_clean = (row.get("UnitType") or "").strip().lower().replace(" ", "").replace("_", "")
    is_epd = ("+" in unit_type_clean or "plus" in unit_type_clean)
    epd_status = "EPD" if is_epd else "Non-EPD"
    epd_para_style = epd_badge_style if is_epd else non_epd_badge_style
    epd_bg = colors.HexColor("#ECFDF5") if is_epd else colors.HexColor("#F1F5F9")

    formatted_delivery_date = format_date_mm_dd_yyyy(row.get("DeliveryDate", ""))

    kpi_cards_data = [
        [
            Paragraph("JOB ORDER ID", kpi_label_style),
            Paragraph("CLIENT NAME", kpi_label_style),
            Paragraph("HARDWARE", kpi_label_style),
            Paragraph("QUANTITY", kpi_label_style),
            Paragraph("DELIVERY", kpi_label_style),
            Paragraph("BUTTON LAYOUT", kpi_label_style),
            Paragraph("DISPLAY", kpi_label_style),
        ],
        [
            Paragraph(f"#{row.get('ID', '-')}", kpi_value_style),
            Paragraph(f"{row.get('ClientName', '-')}", kpi_value_style),
            Paragraph(f"{row.get('UnitType', '-')}", kpi_value_style),
            Paragraph(f"{row.get('TotalQuantity', '-')}", kpi_value_style),
            Paragraph(f"{formatted_delivery_date}", kpi_value_style),
            Paragraph(format_button_layout_2lines(row.get("ButtonLayout", "")), kpi_btn_style),
            Paragraph(f"{epd_status}", epd_para_style),
        ],
    ]
    # Total width = 540 pt: balanced columns with prominent font size
    kpi_table = Table(kpi_cards_data, colWidths=[44, 96, 50, 42, 76, 146, 86])
    kpi_table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#F8FAFC")),
                ("BACKGROUND", (6, 0), (6, -1), epd_bg),
                ("BOX", (0, 0), (-1, -1), 1, colors.HexColor("#CBD5E1")),
                ("BOX", (6, 0), (6, -1), 1, colors.HexColor("#10B981") if is_epd else colors.HexColor("#94A3B8")),
                ("INNERGRID", (0, 0), (-1, -1), 0.5, colors.HexColor("#E2E8F0")),
                ("TOPPADDING", (0, 0), (-1, 0), 4),
                ("BOTTOMPADDING", (0, 0), (-1, 0), 2),
                ("TOPPADDING", (0, 1), (-1, 1), 3),
                ("BOTTOMPADDING", (0, 1), (-1, 1), 4),
                ("LEFTPADDING", (0, 0), (-1, -1), 2),
                ("RIGHTPADDING", (0, 0), (-1, -1), 2),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ]
        )
    )
    elements.append(kpi_table)
    elements.append(Spacer(1, 8))

    # 3. Main Specifications Table (listing all fields from the CSV row)
    elements.append(Paragraph("Order Specifications", section_title_style))

    raw_install_freq = (
        row.get("Install freqeunecy")
        or row.get("Install frequency")
        or row.get("Install Frequency")
        or ""
    ).strip()
    lora_region = format_lora_region(raw_install_freq)

    field_definitions = [
        ("ID", "Order ID Number", row.get("ID", "")),
        ("Install Frequency", "LoRa Region", lora_region),
        ("OrderID", "Order Tracking UUID", row.get("OrderID", "")),
        ("Title", "Order Title", row.get("Title", "")),
        ("Substring", "Prefix Substring", row.get("Substring", "")),
        ("ClientName", "Client / Account Name", row.get("ClientName", "")),
        ("ClientPrefix", "Device Registry Name Prefix", row.get("ClientPrefix", "")),
        ("UnitType", "Hardware Unit Type", row.get("UnitType", "")),
        ("TotalQuantity", "Total Order Quantity", row.get("TotalQuantity", "")),
        ("Language", "Language / EPD Locale", row.get("Language", "")),
        ("DeliveryDate", "Target Delivery Date (MM-DD-YYYY)", format_date_mm_dd_yyyy(row.get("DeliveryDate", ""))),
        ("Timezone", "Target Timezone", row.get("Timezone", "")),
        ("DecalType", "Decal Type / Application", row.get("DecalType", "")),
        ("ButtonLayout", "Button & LED Layout", format_button_layout_spec(row.get("ButtonLayout", ""))),
        ("FacePlateNotes", "Face Plate & Assembly Notes", row.get("FacePlateNotes", "") or "(None)"),
    ]

    table_rows = [
        [
            Paragraph("Order Specification Field", table_header_style),
            Paragraph("Parameter / Setting Value", table_header_style),
        ]
    ]

    for key, friendly_label, val in field_definitions:
        table_rows.append(
            [
                Paragraph(f"{friendly_label} <font color='#64748B' size=7>({key})</font>", field_label_style),
                Paragraph(val or "—", field_value_style),
            ]
        )

    spec_table = Table(table_rows, colWidths=[190, 350])
    spec_table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#0F172A")),
                ("BOTTOMPADDING", (0, 0), (-1, 0), 5),
                ("TOPPADDING", (0, 0), (-1, 0), 5),
                ("ALIGN", (0, 0), (-1, 0), "LEFT"),
                ("GRID", (0, 0), (-1, -1), 0.5, colors.HexColor("#CBD5E1")),
                ("BACKGROUND", (0, 1), (0, -1), colors.HexColor("#F8FAFC")),
                ("ROWBACKGROUNDS", (1, 1), (-1, -1), [colors.white, colors.HexColor("#F8FAFC")]),
                ("TOPPADDING", (0, 1), (-1, -1), 3.5),
                ("BOTTOMPADDING", (0, 1), (-1, -1), 3.5),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ]
        )
    )
    elements.append(spec_table)
    elements.append(Spacer(1, 10))

    # 4. Production & QA Verification Checklist
    elements.append(Paragraph("Production & Quality Assurance Sign-Off", section_title_style))

    chk_col_w = [18, 162, 18, 162, 18, 162]
    checklist_data = [
        [
            Paragraph("☐", checklist_item_style),
            Paragraph("Hardware Variant Verified", checklist_item_style),
            Paragraph("☐", checklist_item_style),
            Paragraph("Firmware Flashed & Verified", checklist_item_style),
            Paragraph("☐", checklist_item_style),
            Paragraph("Decal & Button Plate Applied", checklist_item_style),
        ],
        [
            Paragraph("☐", checklist_item_style),
            Paragraph("LoRaWAN Keys & EUI Registered", checklist_item_style),
            Paragraph("☐", checklist_item_style),
            Paragraph("RTC & Timezone Verified", checklist_item_style),
            Paragraph("☐", checklist_item_style),
            Paragraph("Final Packaged & Labeled", checklist_item_style),
        ],
    ]
    chk_table = Table(checklist_data, colWidths=chk_col_w)
    chk_table.setStyle(
        TableStyle(
            [
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("TOPPADDING", (0, 0), (-1, -1), 2),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
                ("LEFTPADDING", (0, 0), (-1, -1), 0),
                ("RIGHTPADDING", (0, 0), (-1, -1), 0),
            ]
        )
    )
    elements.append(chk_table)
    elements.append(Spacer(1, 8))

    # 5. Technician Sign-off Table
    signoff_data = [
        [
            Paragraph("<b>Production Tech:</b> ___________________________", checklist_item_style),
            Paragraph("<b>Date:</b> ______________", checklist_item_style),
            Paragraph("<b>QA Inspector:</b> ___________________________", checklist_item_style),
            Paragraph("<b>Date:</b> ______________", checklist_item_style),
        ]
    ]
    signoff_table = Table(signoff_data, colWidths=[180, 90, 180, 90])
    signoff_table.setStyle(
        TableStyle(
            [
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("TOPPADDING", (0, 0), (-1, -1), 2),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
            ]
        )
    )
    elements.append(signoff_table)

    # Build the document
    doc.build(elements)
    return output_path


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a Job Order Form PDF from all_box_orders.csv for a given Order ID."
    )
    parser.add_argument(
        "--id",
        "--order-id",
        "--box-order-id",
        dest="target_id",
        default=None,
        help="Order ID number from all_box_orders.csv (e.g. 33)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Generate Job Order Form PDFs for all orders in the CSV",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        default=DEFAULT_CSV_PATH,
        help="Path to all_box_orders.csv (default: %(default)s)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Directory to save generated PDF(s) (default: %(default)s)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    if not args.target_id and not args.all:
        print("ERROR: Please specify an order with --id <ID> or use --all to generate all order forms.", file=sys.stderr)
        print("Example: python3 generate_job_order.py --id 33")
        return 2

    if args.all:
        try:
            rows = read_all_box_orders(args.csv)
            if not rows:
                print(f"No orders found in {args.csv}")
                return 1
            print(f"Generating Job Order Form PDFs for {len(rows)} order(s)...")
            for r in rows:
                filename = build_job_order_filename(r)
                pdf_path = args.output_dir / filename
                generate_pdf_order_form(r, pdf_path)
                print(f"  [ID {r.get('ID', '?'):>2}] Generated: {pdf_path.name}")
            print(f"\nAll PDFs saved in: {args.output_dir.resolve()}")
            return 0
        except Exception as err:
            print(f"ERROR: {err}", file=sys.stderr)
            return 1

    try:
        row = read_box_order_row(args.csv, args.target_id)
        filename = build_job_order_filename(row)
        pdf_path = args.output_dir / filename
        generate_pdf_order_form(row, pdf_path)

        print("\n=== Job Order Form PDF Generated ===")
        print(f"Order ID       : {row.get('ID')}")
        print(f"Client         : {row.get('ClientName')}")
        print(f"Unit Type      : {row.get('UnitType')}")
        print(f"Delivery Date  : {format_date_mm_dd_yyyy(row.get('DeliveryDate', ''))}")
        print(f"Total Quantity : {row.get('TotalQuantity')}")
        print(f"Output File    : {pdf_path.resolve()}\n")
        return 0
    except Exception as err:
        print(f"ERROR: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
