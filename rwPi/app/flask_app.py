"""
Flask web app for Flash workflow: employee name + 4-byte hex + block, Flash!, CSV append.
Runs on 0.0.0.0 so accessible via localhost (7" screen) or RPi IP over Ethernet.
"""
import re
import threading
from pathlib import Path

from flask import Flask, jsonify, render_template, request, send_file

# Project root for template resolution when run as python -m app.flask_app
PROJECT_ROOT = Path(__file__).resolve().parent.parent
import sys
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from app.config import get_config, get_spi_path
from export.csv_export import append_flash_row
from nfc.pn5180 import PN5180, PN5180_OK, PN5180_ERR_TIMEOUT, strerror

APP_DIR = PROJECT_ROOT / "app"
app = Flask(
    __name__,
    template_folder=str(PROJECT_ROOT / "app" / "templates"),
    static_folder=str(APP_DIR),
    static_url_path="/static",
)
nfc_lock = threading.Lock()
_nfc_device = None


def _default_block():
    return get_config().get("default_block", 5)


def _get_nfc():
    """Lazy init PN5180; hold lock when calling."""
    global _nfc_device
    if _nfc_device is not None:
        return _nfc_device
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
    err = dev.init()
    if err != PN5180_OK:
        raise RuntimeError(f"PN5180 init failed: {strerror(err)}")
    _nfc_device = dev
    return _nfc_device


def _validate_hex(s):
    s = (s or "").strip().replace(" ", "")
    return len(s) == 8 and re.match(r"^[0-9A-Fa-f]{8}$", s) is not None


@app.route("/")
def index():
    return render_template("index.html", default_block=_default_block())


def _hexdump_line(block_num: int, data: bytes) -> str:
    """Format one block as 'NN: XX XX XX XX'."""
    hex_bytes = " ".join(f"{b:02X}" for b in (data + bytes(4 - len(data)))[:4])
    return f"{block_num:02d}: {hex_bytes}"


@app.route("/read", methods=["POST"])
def read():
    """Inventory; read blocks 0-27; return UID and hexdump lines (default_block marked)."""
    default_block = _default_block()
    with nfc_lock:
        try:
            dev = _get_nfc()
        except RuntimeError as e:
            return jsonify({"success": False, "error": str(e)}), 500
        err, uid = dev.get_inventory()
        if err != PN5180_OK:
            msg = "No tag detected" if err == PN5180_ERR_TIMEOUT else strerror(err)
            return jsonify({"success": False, "error": msg}), 200
        uid_hex = uid.hex().upper()
        lines = []
        for b in range(28):
            err, data = dev.read_block(uid, b, 4)
            if err != PN5180_OK:
                lines.append({"block": b, "text": f"{b:02d}: ---- (read error)", "highlight": b == default_block})
            else:
                lines.append({"block": b, "text": _hexdump_line(b, data), "highlight": b == default_block})
    return jsonify({
        "success": True,
        "uid": uid_hex,
        "hexdump": lines,
        "default_block": default_block,
    })


@app.route("/flash", methods=["POST"])
def flash():
    """Validate; inventory; write default block; on success append CSV and return JSON."""
    try:
        data = request.get_json(silent=True) or request.form
        employee_name = (data.get("employee_name") or "").strip()
        data_hex = (data.get("data_hex") or "").strip().replace(" ", "")
        block = _default_block()

        if not employee_name:
            return jsonify({"success": False, "error": "Employee name is required"}), 400
        if not _validate_hex(data_hex):
            return jsonify({"success": False, "error": "Data must be exactly 8 hex characters (e.g. AABBCCDD)"}), 400

        with nfc_lock:
            try:
                dev = _get_nfc()
            except RuntimeError as e:
                return jsonify({"success": False, "error": str(e)}), 500
            err, uid = dev.get_inventory()
            if err != PN5180_OK:
                msg = "No tag detected" if err == PN5180_ERR_TIMEOUT else strerror(err)
                return jsonify({"success": False, "error": msg}), 200
            uid_hex = uid.hex().upper()
            payload = bytes.fromhex(data_hex)
            err = dev.write_block(uid, block, payload)
        if err != PN5180_OK:
            msg = "No tag detected" if err == PN5180_ERR_TIMEOUT else strerror(err)
            return jsonify({"success": False, "error": msg, "uid": uid_hex}), 200

        cfg = get_config()
        csv_path = cfg.get("csv_path") or str(PROJECT_ROOT / "nfc_log.csv")
        try:
            append_flash_row(csv_path, uid_hex=uid_hex, employee_name=employee_name, block_number=block, data_hex=data_hex)
        except Exception as e:
            return jsonify({"success": False, "error": f"CSV write failed: {e}", "uid": uid_hex}), 200

        return jsonify({"success": True, "uid": uid_hex, "block": block})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500


@app.route("/csv")
def download_csv():
    """Send CSV file as attachment or 404."""
    cfg = get_config()
    csv_path = cfg.get("csv_path") or str(PROJECT_ROOT / "nfc_log.csv")
    p = Path(csv_path)
    if not p.exists():
        return "", 404
    return send_file(p, as_attachment=True, download_name=p.name, mimetype="text/csv")


def main():
    import argparse
    parser = argparse.ArgumentParser(description="rwPi Flash web app")
    parser.add_argument("--port", type=int, default=5000, help="Port (default 5000)")
    parser.add_argument("--host", default="0.0.0.0", help="Host (default 0.0.0.0)")
    args = parser.parse_args()
    app.run(host=args.host, port=args.port, debug=False, threaded=True)


if __name__ == "__main__":
    main()
