"""
Application configuration: SPI bus, GPIO pins, CSV path, timeouts.
Loads from config/default.yaml if present; otherwise uses defaults.
"""
import os
from pathlib import Path

# Base path: directory containing rwPi (project root)
PROJECT_ROOT = Path(__file__).resolve().parent.parent
CONFIG_DIR = PROJECT_ROOT / "config"
DEFAULT_CONFIG_PATH = CONFIG_DIR / "default.yaml"

# Defaults (match lgpio_eeprom.py: BUSY=27, RST=17, NSS=22; avoid GPIO 8 = SPI CE0)
DEFAULTS = {
    "spi_bus": 0,
    "spi_device": 0,
    "gpio_nss": 22,   # manual CS (active LOW) — avoid 8 (SPI CE0)
    "gpio_rst": 17,
    "gpio_busy": 27,
    "gpio_irq": None,
    "timeout_ms": 1000,
    "spi_speed_hz": 4_000_00,
    "csv_path": str(PROJECT_ROOT / "nfc_log.csv"),
    "default_block": 5,  # block used for write; user cannot choose
}


def _load_yaml_if_available():
    """Load config from default.yaml if file exists and PyYAML is available."""
    if not DEFAULT_CONFIG_PATH.exists():
        return {}
    try:
        import yaml
        with open(DEFAULT_CONFIG_PATH, "r") as f:
            return yaml.safe_load(f) or {}
    except Exception:
        return {}


def _env_overrides():
    """Environment variable overrides (e.g. RWPI_SPI_BUS=0)."""
    overrides = {}
    for key in ("spi_bus", "spi_device", "gpio_nss", "gpio_rst", "gpio_busy", "gpio_irq", "timeout_ms", "spi_speed_hz", "default_block"):
        env_key = f"RWPI_{key.upper()}"
        val = os.environ.get(env_key)
        if val is not None:
            try:
                overrides[key] = int(val)
            except ValueError:
                pass
    csv_path = os.environ.get("RWPI_CSV_PATH")
    if csv_path is not None:
        overrides["csv_path"] = csv_path
    return overrides


def get_config():
    """
    Return merged config: DEFAULTS <- default.yaml <- env.
    All pin/bus values are integers except csv_path (str).
    """
    cfg = dict(DEFAULTS)
    yaml_cfg = _load_yaml_if_available()
    for k, v in yaml_cfg.items():
        if k in cfg and v is not None:
            cfg[k] = v
    for k, v in _env_overrides().items():
        cfg[k] = v
    return cfg


def get_spi_path(config=None):
    """Return SPI device path e.g. /dev/spidev0.0."""
    cfg = config or get_config()
    return f"/dev/spidev{cfg['spi_bus']}.{cfg['spi_device']}"
