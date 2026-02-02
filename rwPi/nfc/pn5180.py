"""
PN5180 low-level driver: SPI, GPIO, registers, RF, EEPROM.
Ported from Zephyr driver; same command bytes and sequencing.
"""
import struct
import time
from typing import Optional, Tuple

# --- PN5180 Commands ---
PN5180_WRITE_REGISTER = 0x00
PN5180_WRITE_REGISTER_OR_MASK = 0x01
PN5180_WRITE_REGISTER_AND_MASK = 0x02
PN5180_READ_REGISTER = 0x04
PN5180_WRITE_EEPROM = 0x06
PN5180_READ_EEPROM = 0x07
PN5180_SEND_DATA = 0x09
PN5180_READ_DATA = 0x0A
PN5180_LOAD_RF_CONFIG = 0x11
PN5180_RF_ON = 0x16
PN5180_RF_OFF = 0x17

# --- EEPROM addresses ---
PN5180_EEPROM_DIE_ID = 0x00
PN5180_EEPROM_PRODUCT_VERSION = 0x10
PN5180_EEPROM_FIRMWARE_VERSION = 0x12
PN5180_EEPROM_EEPROM_VERSION = 0x14

# --- Registers ---
SYSTEM_CONFIG = 0x00
IRQ_ENABLE = 0x01
IRQ_STATUS = 0x02
IRQ_CLEAR = 0x03
RX_STATUS = 0x13
TX_WAIT_CONFIG = 0x17
TX_CONFIG = 0x18

# --- IRQ bits ---
RX_IRQ_STAT = 1 << 0
TX_IRQ_STAT = 1 << 1
IDLE_IRQ_STAT = 1 << 2
RFOFF_DET_IRQ_STAT = 1 << 6
RFON_DET_IRQ_STAT = 1 << 7
TX_RFOFF_IRQ_STAT = 1 << 8
TX_RFON_IRQ_STAT = 1 << 9
RX_SOF_DET_IRQ_STAT = 1 << 14

# --- RX_STATUS bits ---
RX_STATUS_LEN_MASK = 0x000001FF
RX_STATUS_COLLISION_DET = 1 << 18
RX_STATUS_PROTOCOL_ERR = 1 << 19
RX_STATUS_CRC_OK = 1 << 20
RX_STATUS_DATA_INTEGRITY_ERR = 1 << 21

# --- ISO15693 (for issue_iso15693_command) ---
ISO15693_EC_NO_CARD = -10
ISO15693_EC_COLLISION = -11
ISO15693_EC_CRC_ERROR = -13
ISO15693_EC_INVALID_RESPONSE = -14
ISO15693_EC_CUSTOM_CMD_ERROR = 0xA0

# --- Constants ---
PN5180_MAX_RX_LENGTH = 508
PN5180_READ_BUFFER_SIZE = 10
PN5180_POLL_INTERVAL_US = 50
PN5180_RESET_DELAY_MS = 10
PN5180_POST_RESET_DELAY_MS = 50
PN5180_CS_GUARD_DELAY_MS = 5

PN5180_OK = 0
PN5180_ERR_TIMEOUT = -1
PN5180_ERR_SPI = -2
PN5180_ERR_GPIO = -3
PN5180_ERR_INVALID_PARAM = -4


def _le32(val: int) -> bytes:
    return struct.pack("<I", val & 0xFFFFFFFF)


def _parse_le32(buf: bytes) -> int:
    return struct.unpack("<I", buf[:4])[0]


def strerror(code: int) -> str:
    """Human-readable error string for PN5180/ISO15693 codes."""
    if code == PN5180_OK:
        return "OK"
    if code == PN5180_ERR_TIMEOUT:
        return "Timeout"
    if code == PN5180_ERR_SPI:
        return "SPI communication error"
    if code == PN5180_ERR_GPIO:
        return "GPIO error"
    if code == PN5180_ERR_INVALID_PARAM:
        return "Invalid parameter"
    if code == ISO15693_EC_NO_CARD:
        return "No card detected"
    if code == ISO15693_EC_COLLISION:
        return "Collision detected"
    if code == ISO15693_EC_CRC_ERROR:
        return "CRC error"
    if code == ISO15693_EC_INVALID_RESPONSE:
        return "Invalid response format"
    if 0xA0 <= code <= 0xDF:
        return "Custom command error"
    return "Unknown error code"


class PN5180:
    """
    PN5180 driver: software NSS, BUSY polling, same sequencing as Zephyr.
    """

    def __init__(
        self,
        spi_path: str = "/dev/spidev0.0",
        gpio_nss: int = 22,
        gpio_rst: int = 17,
        gpio_busy: int = 27,
        gpio_irq: Optional[int] = None,
        timeout_ms: int = 1000,
        spi_speed_hz: int = 1_000_000,
    ):
        self.spi_path = spi_path
        self.gpio_nss = gpio_nss
        self.gpio_rst = gpio_rst
        self.gpio_busy = gpio_busy
        self.gpio_irq = gpio_irq
        self.timeout_ms = timeout_ms
        self.spi_speed_hz = spi_speed_hz
        self._spi = None
        self._gpio_ready = False
        self._initialized = False
        self._gpio_backend: Optional[str] = None  # "lgpio" or "rpi"
        self._lgpio_handle = None

    def _cs_low(self) -> None:
        """Assert CS (active LOW) = select device."""
        if self._gpio_backend == "lgpio":
            __import__("lgpio").gpio_write(self._lgpio_handle, self.gpio_nss, 0)
        else:
            self._gpio.output(self.gpio_nss, 0)

    def _cs_high(self) -> None:
        """Deassert CS = deselect."""
        if self._gpio_backend == "lgpio":
            __import__("lgpio").gpio_write(self._lgpio_handle, self.gpio_nss, 1)
        else:
            self._gpio.output(self.gpio_nss, 1)

    def _busy_read(self) -> int:
        if self._gpio_backend == "lgpio":
            return __import__("lgpio").gpio_read(self._lgpio_handle, self.gpio_busy)
        return self._gpio.input(self.gpio_busy)

    def _wait_until_available(self, timeout_ms: int = 50) -> bool:
        deadline = time.monotonic() + timeout_ms / 1000.0
        while self._busy_read():
            if time.monotonic() > deadline:
                return False
            time.sleep(PN5180_POLL_INTERVAL_US / 1e6)
        return True

    def _wait_until_busy(self, timeout_ms: int = 50) -> bool:
        deadline = time.monotonic() + timeout_ms / 1000.0
        while not self._busy_read():
            if time.monotonic() > deadline:
                return False
            time.sleep(PN5180_POLL_INTERVAL_US / 1e6)
        return True

    def _spi_send(self, data: bytes) -> int:
        if not self._wait_until_available(50):
            self._cs_high()
            return PN5180_ERR_TIMEOUT
        self._cs_low()
        try:
            self._spi.writebytes(list(data))
        except Exception:
            self._cs_high()
            return PN5180_ERR_SPI
        if not self._wait_until_busy(50):
            self._cs_high()
            return PN5180_ERR_TIMEOUT
        self._cs_high()
        time.sleep(PN5180_CS_GUARD_DELAY_MS / 1000.0)
        if not self._wait_until_available(50):
            self._cs_high()
            return PN5180_ERR_TIMEOUT
        self._cs_high()
        return PN5180_OK

    def _spi_read(self, length: int) -> Tuple[int, bytes]:
        if not self._wait_until_available(50):
            self._cs_high()
            return PN5180_ERR_TIMEOUT, b""
        self._cs_low()
        try:
            out = bytes(self._spi.readbytes(length))
        except Exception:
            self._cs_high()
            return PN5180_ERR_SPI, b""
        if not self._wait_until_busy(50):
            self._cs_high()
            return PN5180_ERR_TIMEOUT, b""
        self._cs_high()
        time.sleep(PN5180_CS_GUARD_DELAY_MS / 1000.0)
        if not self._wait_until_available(50):
            self._cs_high()
            return PN5180_ERR_TIMEOUT, b""
        self._cs_high()
        return PN5180_OK, out

    def init(self) -> int:
        """Open SPI, configure GPIO (lgpio preferred, else RPi.GPIO), reset PN5180."""
        try:
            import spidev
            self._spi = spidev.SpiDev()
            bus, dev = self.spi_path.replace("/dev/spidev", "").strip().split(".")
            self._spi.open(int(bus), int(dev))
            self._spi.max_speed_hz = self.spi_speed_hz
            self._spi.mode = 0
            self._spi.bits_per_word = 8
            self._spi.no_cs = True
        except Exception:
            return PN5180_ERR_SPI

        # Prefer lgpio (works without root on newer Pi OS)
        try:
            import lgpio
            self._lgpio_handle = lgpio.gpiochip_open(0)
            if self._lgpio_handle < 0:
                raise RuntimeError("gpiochip_open(0) failed")
            lgpio.gpio_claim_input(self._lgpio_handle, self.gpio_busy)
            lgpio.gpio_claim_output(self._lgpio_handle, self.gpio_rst, 1)
            lgpio.gpio_claim_output(self._lgpio_handle, self.gpio_nss, 1)  # CS inactive HIGH
            if self.gpio_irq is not None:
                lgpio.gpio_claim_input(self._lgpio_handle, self.gpio_irq)
            self._gpio_backend = "lgpio"
            self._gpio_ready = True
        except Exception as e:
            try:
                import RPi.GPIO as GPIO
                self._gpio = GPIO
                self._gpio.setwarnings(False)
                self._gpio.setmode(GPIO.BCM)
                self._gpio.setup(self.gpio_nss, GPIO.OUT, initial=GPIO.HIGH)  # CS inactive HIGH
                self._gpio.setup(self.gpio_rst, GPIO.OUT, initial=GPIO.HIGH)
                self._gpio.setup(self.gpio_busy, GPIO.IN)
                if self.gpio_irq is not None:
                    self._gpio.setup(self.gpio_irq, GPIO.IN)
                self._gpio_backend = "rpi"
                self._gpio_ready = True
            except Exception as e2:
                if self._spi:
                    try:
                        self._spi.close()
                    except Exception:
                        pass
                print("GPIO init error (lgpio and RPi.GPIO failed):", e, e2, file=__import__("sys").stderr)
                return PN5180_ERR_GPIO

        ret = self.reset()
        if ret != PN5180_OK:
            return ret
        self._initialized = True
        return PN5180_OK

    def reset(self) -> int:
        if self._gpio_backend == "lgpio":
            lgpio = __import__("lgpio")
            lgpio.gpio_write(self._lgpio_handle, self.gpio_rst, 0)
        else:
            self._gpio.output(self.gpio_rst, 0)
        time.sleep(PN5180_RESET_DELAY_MS / 1000.0)
        if self._gpio_backend == "lgpio":
            lgpio = __import__("lgpio")
            lgpio.gpio_write(self._lgpio_handle, self.gpio_rst, 1)
        else:
            self._gpio.output(self.gpio_rst, 1)
        time.sleep(PN5180_POST_RESET_DELAY_MS / 1000.0)
        return PN5180_OK

    def read_register(self, reg: int) -> Tuple[int, int]:
        """Returns (err, value). Value is 32-bit."""
        ret = self._spi_send(bytes([PN5180_READ_REGISTER, reg]))
        if ret != PN5180_OK:
            return ret, 0
        ret, buf = self._spi_read(4)
        if ret != PN5180_OK or len(buf) < 4:
            return ret if ret != PN5180_OK else PN5180_ERR_SPI, 0
        return PN5180_OK, _parse_le32(buf)

    def write_register(self, reg: int, value: int) -> int:
        payload = _le32(value)
        return self._spi_send(bytes([PN5180_WRITE_REGISTER, reg]) + payload)

    def load_iso15693_config(self) -> int:
        return self._spi_send(bytes([PN5180_LOAD_RF_CONFIG, 0x0D, 0x8D]))

    def clear_irq(self) -> int:
        return self._spi_send(
            bytes([PN5180_WRITE_REGISTER, IRQ_CLEAR, 0xFF, 0xFF, 0x0F, 0x00])
        )

    def set_idle(self) -> int:
        return self._spi_send(
            bytes([PN5180_WRITE_REGISTER_AND_MASK, SYSTEM_CONFIG, 0xF8, 0xFF, 0xFF, 0xFF])
        )

    def activate_transceive(self) -> int:
        return self._spi_send(
            bytes([PN5180_WRITE_REGISTER_OR_MASK, SYSTEM_CONFIG, 0x03, 0x00, 0x00, 0x00])
        )

    def activate_rf(self) -> int:
        ret = self._spi_send(bytes([PN5180_RF_ON, 0x00]))
        if ret != PN5180_OK:
            return ret
        deadline = time.monotonic() + self.timeout_ms / 1000.0
        while True:
            err, irq = self.read_register(IRQ_STATUS)
            if err != PN5180_OK:
                return err
            if irq & TX_RFON_IRQ_STAT:
                break
            if time.monotonic() > deadline:
                return PN5180_ERR_TIMEOUT
            time.sleep(0.005)
        return self._spi_send(
            bytes([PN5180_WRITE_REGISTER, IRQ_CLEAR])
            + _le32(TX_RFON_IRQ_STAT)
        )

    def disable_rf(self) -> int:
        ret = self._spi_send(bytes([PN5180_RF_OFF, 0x00]))
        if ret != PN5180_OK:
            return ret
        deadline = time.monotonic() + self.timeout_ms / 1000.0
        while True:
            err, irq = self.read_register(IRQ_STATUS)
            if err != PN5180_OK:
                return err
            if irq & TX_RFOFF_IRQ_STAT:
                break
            if time.monotonic() > deadline:
                return PN5180_ERR_TIMEOUT
            time.sleep(0.005)
        return self._spi_send(
            bytes([PN5180_WRITE_REGISTER, IRQ_CLEAR]) + _le32(TX_RFOFF_IRQ_STAT)
        )

    def read_eeprom(self, addr: int, length: int) -> Tuple[int, bytes]:
        """Read EEPROM at addr for length bytes (max 255). Returns (err, data)."""
        if length <= 0 or length > 255:
            return PN5180_ERR_INVALID_PARAM, b""
        ret = self._spi_send(bytes([PN5180_READ_EEPROM, addr, length & 0xFF]))
        if ret != PN5180_OK:
            return ret, b""
        ret, data = self._spi_read(length)
        return ret, data

    def get_version(self) -> Tuple[int, Optional[dict]]:
        """
        Read product/firmware/eeprom version from EEPROM.
        Returns (err, info_dict) with product_version, firmware_version, eeprom_version (each 16-bit).
        """
        data = [0] * 6
        err, buf = self.read_eeprom(PN5180_EEPROM_PRODUCT_VERSION, 2)
        if err != PN5180_OK:
            return err, None
        data[0:2] = buf[0], buf[1]
        err, buf = self.read_eeprom(PN5180_EEPROM_FIRMWARE_VERSION, 2)
        if err != PN5180_OK:
            return err, None
        data[2:4] = buf[0], buf[1]
        err, buf = self.read_eeprom(PN5180_EEPROM_EEPROM_VERSION, 2)
        if err != PN5180_OK:
            return err, None
        data[4:6] = buf[0], buf[1]
        return PN5180_OK, {
            "product_version": data[0] | (data[1] << 8),
            "firmware_version": data[2] | (data[3] << 8),
            "eeprom_version": data[4] | (data[5] << 8),
        }

    def _read_reception_buffer(self, length: int) -> Tuple[int, bytes]:
        if length <= 0 or length > PN5180_MAX_RX_LENGTH:
            return PN5180_ERR_INVALID_PARAM, b""
        ret = self._spi_send(bytes([PN5180_READ_DATA, 0x00]))
        if ret != PN5180_OK:
            return ret, b""
        return self._spi_read(length)

    def _send_end_of_frame(self) -> int:
        ret = self._spi_send(
            bytes([PN5180_WRITE_REGISTER_AND_MASK, TX_CONFIG, 0x3F, 0xFB, 0xFF, 0xFF])
        )
        if ret != PN5180_OK:
            return ret
        return self._spi_send(bytes([PN5180_SEND_DATA, 0x00]))

    def issue_iso15693_command(
        self, iso_cmd: bytes, response_max_len: int = 64
    ) -> Tuple[int, bytes]:
        """
        Send ISO15693 command and wait for response.
        Returns (err, response_bytes). err < 0 on failure; on success err is length and response_bytes filled.
        """
        if len(iso_cmd) > 30:
            return PN5180_ERR_INVALID_PARAM, b""
        send_cmd = bytes([PN5180_SEND_DATA, 0x00]) + iso_cmd
        self.clear_irq()
        self.set_idle()
        self.activate_transceive()
        ret = self._spi_send(send_cmd)
        if ret != PN5180_OK:
            return ret, b""
        time.sleep(0.01)
        deadline = time.monotonic() + self.timeout_ms / 1000.0
        while True:
            err, irq = self.read_register(IRQ_STATUS)
            if err != PN5180_OK:
                return err, b""
            if irq & RX_IRQ_STAT:
                break
            if time.monotonic() > deadline:
                return ISO15693_EC_NO_CARD, b""
            time.sleep(0.005)
        err, rx_status = self.read_register(RX_STATUS)
        if err != PN5180_OK:
            return err, b""
        if rx_status & RX_STATUS_COLLISION_DET:
            return ISO15693_EC_COLLISION, b""
        if rx_status & RX_STATUS_PROTOCOL_ERR:
            return -0x0F, b""  # ISO15693_EC_UNKNOWN_ERROR
        if rx_status & RX_STATUS_DATA_INTEGRITY_ERR:
            return ISO15693_EC_CRC_ERROR, b""
        length = rx_status & RX_STATUS_LEN_MASK
        if length == 0:
            return ISO15693_EC_NO_CARD, b""
        if length > response_max_len:
            length = response_max_len
        err, resp = self._read_reception_buffer(length)
        if err != PN5180_OK:
            return err, b""
        if len(resp) < 1:
            return ISO15693_EC_INVALID_RESPONSE, b""
        if resp[0] & 0x01:
            error_byte = resp[1] if len(resp) > 1 else 0
            if 0xA0 <= error_byte <= 0xDF:
                return ISO15693_EC_CUSTOM_CMD_ERROR, b""
            if 0x01 <= error_byte <= 0x14:
                return error_byte, b""
            return -0x0F, b""
        self.clear_irq()
        return len(resp), resp

    def get_inventory(self, verbose: bool = False) -> Tuple[int, bytes]:
        """
        Perform ISO15693 inventory; returns (err, uid_8_bytes).
        UID is MSB-first. Matches Zephyr driver flow exactly.
        If verbose=True, print RX_STATUS and length per slot to stderr.
        """
        import sys
        if not self._initialized:
            return PN5180_ERR_INVALID_PARAM, b""
        ret = self.load_iso15693_config()
        if ret != PN5180_OK:
            return ret, b""
        ret = self.activate_rf()
        if ret != PN5180_OK:
            return ret, b""
        time.sleep(0.05)  # RF field stabilization before inventory
        # Zephyr: clear_irq, set_idle, activate_transceive, send_inventory_cmd
        self.clear_irq()
        self.set_idle()
        self.activate_transceive()
        self._spi_send(bytes([PN5180_SEND_DATA, 0x00, 0x06, 0x01, 0x00]))
        time.sleep(0.02)  # let TX complete and tag receive request
        tag_detected = False
        uid = bytearray(8)
        for slot in range(16):
            # Wait for RX_IRQ_STAT (reception complete) before reading RX_STATUS, like issue_iso15693_command
            slot_deadline = time.monotonic() + 0.05
            while time.monotonic() < slot_deadline:
                err, irq = self.read_register(IRQ_STATUS)
                if err != PN5180_OK:
                    break
                if irq & RX_IRQ_STAT:
                    break
                time.sleep(0.002)
            err, rx_status = self.read_register(RX_STATUS)
            if err != PN5180_OK:
                if verbose:
                    print(f"[slot {slot}] read_register RX_STATUS err={err}", file=sys.stderr)
                break
            length = rx_status & 0x01FF
            if length > PN5180_READ_BUFFER_SIZE:
                length = PN5180_READ_BUFFER_SIZE
            if verbose:
                print(f"[slot {slot}] RX_STATUS=0x{rx_status:08X} len={length}", file=sys.stderr)
            if length > 0:
                err, buf = self._read_reception_buffer(length)
                if verbose:
                    print(f"[slot {slot}] buf len={len(buf) if buf else 0} hex={buf.hex() if buf else ''} buf[0]={buf[0] if buf else '?'}", file=sys.stderr)
                if err != PN5180_OK:
                    pass
                elif len(buf) >= 10 and buf[0] == 0x00:
                    raw_uid = buf[2:10]
                    for i in range(8):
                        uid[i] = raw_uid[7 - i]
                    tag_detected = True
                    break
                self.clear_irq()  # clear RX_IRQ after reading buffer
            if tag_detected:
                break
            if slot < 15:
                self.set_idle()
                self.activate_transceive()
                self.clear_irq()
                self._send_end_of_frame()
        self.disable_rf()
        if not tag_detected:
            return PN5180_ERR_TIMEOUT, b""
        return PN5180_OK, bytes(uid)

    def read_block(self, uid: bytes, block_num: int, block_size: int = 4) -> Tuple[int, bytes]:
        """Read one block from tag. uid 8 bytes MSB-first. Returns (err, block_data)."""
        if not self._initialized or len(uid) != 8:
            return PN5180_ERR_INVALID_PARAM, b""
        ret = self.load_iso15693_config()
        if ret != PN5180_OK:
            return ret, b""
        ret = self.activate_rf()
        if ret != PN5180_OK:
            return ret, b""
        cmd = bytearray(11)
        cmd[0] = 0x22
        cmd[1] = 0x20
        for i in range(8):
            cmd[2 + i] = uid[7 - i]
        cmd[10] = block_num & 0xFF
        err, resp = self.issue_iso15693_command(bytes(cmd), 32)
        self.disable_rf()
        if err < 0:
            return err, b""
        if err < 1 + block_size:
            return -0x0F, b""
        return PN5180_OK, bytes(resp[1 : 1 + block_size])

    def write_block(
        self, uid: bytes, block_num: int, block_data: bytes, block_size: Optional[int] = None
    ) -> int:
        """Write one block (4 bytes typical). uid 8 bytes MSB-first."""
        if block_size is None:
            block_size = len(block_data)
        if not self._initialized or len(uid) != 8 or len(block_data) < block_size or block_size > 32:
            return PN5180_ERR_INVALID_PARAM
        ret = self.load_iso15693_config()
        if ret != PN5180_OK:
            return ret
        ret = self.activate_rf()
        if ret != PN5180_OK:
            return ret
        cmd = bytearray(11 + block_size)
        cmd[0] = 0x22
        cmd[1] = 0x21
        for i in range(8):
            cmd[2 + i] = uid[7 - i]
        cmd[10] = block_num & 0xFF
        cmd[11 : 11 + block_size] = block_data[:block_size]
        err, _ = self.issue_iso15693_command(bytes(cmd), 8)
        self.disable_rf()
        return err if err < 0 else PN5180_OK

    def close(self) -> None:
        if getattr(self, "_spi", None):
            try:
                self._spi.close()
            except Exception:
                pass
            self._spi = None
        if getattr(self, "_gpio_backend", None) == "lgpio" and getattr(self, "_lgpio_handle", None) is not None:
            try:
                __import__("lgpio").gpiochip_close(self._lgpio_handle)
            except Exception:
                pass
            self._lgpio_handle = None
        elif getattr(self, "_gpio_ready", False) and getattr(self, "_gpio_backend", None) == "rpi" and hasattr(self, "_gpio"):
            try:
                self._gpio.cleanup([self.gpio_nss, self.gpio_rst, self.gpio_busy])
                if self.gpio_irq is not None:
                    self._gpio.cleanup([self.gpio_irq])
            except Exception:
                pass
        self._initialized = False
