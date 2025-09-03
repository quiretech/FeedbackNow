#include "pn5180.h"
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pn5180, LOG_LEVEL_INF);

#define MY_SPI_MASTER DT_NODELABEL(my_spi_master)
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static struct spi_cs_control pn5180_cs = {
    .gpio = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, nfc_nss_gpios),
    .delay = 5,
};

static struct pn5180_cfg nfc_dev = {
    .spi_dev = DEVICE_DT_GET(MY_SPI_MASTER),
    .spi_cfg =
        {
            .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB, // Mode 0
            .frequency = 4000000,
            .slave = 0,
            .cs = NULL, // manual CS
        },
    .irq = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, nfc_irq_gpios),
    .rst = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, nfc_rst_gpios),
    .busy = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, nfc_busy_gpios),
};

// Assert CS
static inline void cs_low(void) { gpio_pin_set_dt(&pn5180_cs.gpio, 1); }
// Deassert CS
static inline void cs_high(void) { gpio_pin_set_dt(&pn5180_cs.gpio, 0); }

void pn5180_reset(void) {
  gpio_pin_set_dt(&nfc_dev.rst, 0); //  LOW
  k_msleep(10);
  gpio_pin_set_dt(&nfc_dev.rst, 1); //  HIGH
  k_msleep(50);
}

void pn5180_init(void) {
  // SPI device check
  if (!device_is_ready(nfc_dev.spi_dev)) {
    LOG_ERR("SPI device not ready");
    return;
  }

  // Configure CS
  if (device_is_ready(pn5180_cs.gpio.port)) {
    gpio_pin_configure_dt(&pn5180_cs.gpio, GPIO_OUTPUT_HIGH);
  } else {
    LOG_ERR("CS GPIO not ready");
    return;
  }

  // Configure RST
  if (device_is_ready(nfc_dev.rst.port)) {
    gpio_pin_configure_dt(&nfc_dev.rst, GPIO_OUTPUT_HIGH);
  } else {
    LOG_ERR("RST GPIO not ready");
  }

  // Configure IRQ
  if (device_is_ready(nfc_dev.irq.port)) {
    gpio_pin_configure_dt(&nfc_dev.irq, GPIO_INPUT);
  } else {
    LOG_ERR("IRQ GPIO not ready");
  }

  // Configure BUSY
  if (device_is_ready(nfc_dev.busy.port)) {
    gpio_pin_configure_dt(&nfc_dev.busy, GPIO_INPUT);
  } else {
    LOG_ERR("BUSY GPIO not ready");
  }

  // NSS HIGH
  cs_high();

  // RST HIGH
  gpio_pin_set_dt(&nfc_dev.rst, 1); //  HIGH

  pn5180_reset();
}

/* --- Init/reset --------------------------------------------------------- */
int pn5180_test_gpios(void) {
  LOG_INF("Starting PN5180 GPIO test pattern");

  /* Ensure reset high initially */
  gpio_pin_set_dt(&nfc_dev.rst, 1);
  cs_high();
  k_msleep(20);

  /* --- Reset pin pattern (RST) --- */
  // Pulse train: short, medium, long
  gpio_pin_set_dt(&nfc_dev.rst, 0);
  k_msleep(5);
  gpio_pin_set_dt(&nfc_dev.rst, 1);
  k_msleep(10);

  gpio_pin_set_dt(&nfc_dev.rst, 0);
  k_msleep(20);
  gpio_pin_set_dt(&nfc_dev.rst, 1);
  k_msleep(10);

  gpio_pin_set_dt(&nfc_dev.rst, 0);
  k_msleep(50);
  gpio_pin_set_dt(&nfc_dev.rst, 1);
  k_msleep(20);

  /* --- CS pin pattern (NSS) --- */
  // Repeating fast square wave
  for (int i = 0; i < 8; i++) {
    cs_low();
    k_msleep(5);
    cs_high();
    k_msleep(5);
  }

  /* --- Mixed toggle sequence --- */
  for (int i = 0; i < 20; i++) {
    gpio_pin_set_dt(&nfc_dev.rst, (i % 2));
    cs_low();
    k_msleep(10);
    cs_high();
    k_msleep(10);
  }
  cs_low();
  gpio_pin_set_dt(&nfc_dev.rst, 0);

  LOG_INF("PN5180 GPIO test pattern complete");
  return 0;
}

/* --- Basic SPI write ---------------------------------------------------- */
int pn5180_spi_write(const uint8_t *data, size_t len) {
  struct spi_buf tx_buf = {
      .buf = (void *)data,
      .len = len,
  };
  struct spi_buf_set tx_set = {
      .buffers = &tx_buf,
      .count = 1,
  };

  int ret = spi_write(nfc_dev.spi_dev, &nfc_dev.spi_cfg, &tx_set);
  if (ret) {
    LOG_ERR("pn5180_spi_write failed: %d", ret);
    return ret;
  }

  LOG_DBG("pn5180_spi_write OK, %u bytes", len);
  return 0;
}

int pn5180_spi_transceive(const uint8_t *tx_data, size_t tx_len,
                          uint8_t *rx_data, size_t rx_len) {
  struct spi_buf tx_buf = {
      .buf = (void *)tx_data,
      .len = tx_len,
  };
  struct spi_buf rx_buf = {
      .buf = rx_data,
      .len = rx_len,
  };

  struct spi_buf_set tx_set = {
      .buffers = &tx_buf,
      .count = 1,
  };
  struct spi_buf_set rx_set = {
      .buffers = &rx_buf,
      .count = 1,
  };

  int ret = spi_transceive(nfc_dev.spi_dev, &nfc_dev.spi_cfg, &tx_set, &rx_set);
  if (ret) {
    LOG_ERR("pn5180_spi_transceive failed: %d", ret);
    return ret;
  }

  LOG_DBG("pn5180_spi_transceive OK, tx=%u, rx=%u", tx_len, rx_len);
  return 0;
}

static bool wait_until_available(k_timeout_t timeout) {
  int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);

  while (gpio_pin_get_dt(&nfc_dev.busy)) {
    if (k_uptime_get() > deadline) {
      LOG_ERR("Timeout: PN5180 still busy (expected LOW)");
      return false;
    }
    k_msleep(50); // poll every 50 µs
  }
  return true;
}

static bool wait_until_busy(k_timeout_t timeout) {
  int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);

  while (!gpio_pin_get_dt(&nfc_dev.busy)) {
    if (k_uptime_get() > deadline) {
      LOG_ERR("Timeout: PN5180 never went busy (expected HIGH)");
      return false;
    }
    k_msleep(50);
  }
  return true;
}

bool pn5180_spi_send_bytes(uint8_t *send_buf, size_t send_len) {
  int ret;

  // 0. Wait until available
  if (!wait_until_available(K_MSEC(50))) {
    cs_high();
    return false;
  }

  // 1. Assert NSS
  cs_low();
  // k_msleep(5);

  // 2. SPI write
  struct spi_buf tx_buf = {.buf = send_buf, .len = send_len};
  struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
  ret = spi_write(nfc_dev.spi_dev, &nfc_dev.spi_cfg, &tx_set);
  if (ret) {
    LOG_ERR("spi_write failed: %d", ret);
    return false;
  }

  // 3. Wait until BUSY = 1
  if (!wait_until_busy(K_MSEC(50))) {
    cs_high();
    return false;
  }

  // 4. Deassert NSS
  cs_high();
  // k_msleep(5);

  // 5. Wait until BUSY = 0
  if (!wait_until_available(K_MSEC(50))) {
    cs_high();
    return false;
  }
  cs_high();

  return true;
}

bool pn5180_spi_read_bytes(uint8_t *recv_buf, size_t recv_len) {
  int ret;

  // 0. Clear buffer
  memset(recv_buf, 0x00, recv_len);

  // 1. Wait until PN5180 is available (BUSY = 0)
  if (!wait_until_available(K_MSEC(50))) {
    cs_high();
    return false;
  }

  // 2. Assert NSS (manual CS)
  // 1. Assert NSS
  cs_low();
  // k_msleep(5); // small guard delay

  // 3. SPI transfer (read)
  struct spi_buf rx_buf = {.buf = recv_buf, .len = recv_len};
  struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
  ret = spi_read(nfc_dev.spi_dev, &nfc_dev.spi_cfg, &rx_set);
  if (ret) {
    LOG_ERR("SPI read failed: %d", ret);
    return false;
  }

  if (!wait_until_busy(K_MSEC(50))) {
    cs_high();
    return false;
  }

  // 4. Deassert NSS
  cs_high();
  k_msleep(5);

  // 5. Wait until BUSY = 0
  if (!wait_until_available(K_MSEC(50))) {
    cs_high();
    return false;
  }
  cs_high();

  return true;
}

bool pn5180_read_register(uint8_t reg_addr, uint32_t *reg_value) {
  uint8_t cmd[] = {PN5180_READ_REGISTER, reg_addr};
  uint8_t buf[4];

  // 1. Send READ_REGISTER command + register address
  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to send READ_REGISTER command");
    return false;
  }

  // 2. Read 4-byte register value
  if (!pn5180_spi_read_bytes(buf, sizeof(buf))) {
    LOG_ERR("Failed to read register value");
    return false;
  }

  // 3. Copy bytes into uint32_t (assume little-endian)
  memcpy(reg_value, buf, sizeof(buf));

  LOG_DBG("Read register 0x%02X = 0x%08X", reg_addr, *reg_value);
  return true;
}

int pn5180_clear_irq(void) {
  uint8_t cmd[] = {PN5180_WRITE_REGISTER, IRQ_CLEAR, 0xFF, 0xFF, 0x0F, 0x00};

  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to clear IRQ_STATUS");
    return -EIO;
  }

  LOG_DBG("IRQ_STATUS cleared");
  return 0;
}
int pn5180_load_iso15693_config(void) {
  uint8_t cmd[] = {PN5180_LOAD_RF_CONFIG, 0x0D, 0x8D};

  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to load ISO15693 RF config");
    return -EIO;
  }
  LOG_DBG("ISO15693 RF config loaded");
  return 0;
}

/* Set the PN5180 into IDLE state */
int pn5180_set_idle(void) {
  uint8_t cmd[] = {
      PN5180_WRITE_REGISTER_AND_MASK, SYSTEM_CONFIG, 0xF8, 0xFF, 0xFF, 0xFF};

  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to set IDLE state");
    return -EIO;
  }
  LOG_DBG("PN5180 set to IDLE");
  return 0;
}

/* Activate TRANSCEIVE routine */
int pn5180_activate_transceive(void) {
  uint8_t cmd[] = {
      PN5180_WRITE_REGISTER_OR_MASK, SYSTEM_CONFIG, 0x03, 0x00, 0x00, 0x00};

  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to activate TRANSCEIVE");
    return -EIO;
  }
  LOG_DBG("PN5180 TRANSCEIVE activated");
  return 0;
}

int pn5180_send_inventory_cmd(void) {
  uint8_t cmd[] = {
      PN5180_SEND_DATA,
      0x00, // framing / reserved
      0x06, // ISO15693 command length?
      0x01, // Inventory command code
      0x00  // flags / options
  };

  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to send ISO15693 Inventory command");
    return -EIO;
  }

  LOG_DBG("ISO15693 Inventory command sent");
  return 0;
}

int pn5180_activate_rf(void) {
  uint8_t cmd[] = {PN5180_RF_ON, 0x00};

  // 1. Send RF_ON command
  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to send RF_ON command");
    return -EIO;
  }

  // 2. Wait until TX_RFON_IRQ_STAT is set (timeout 500 ms)
  int64_t start_time = k_uptime_get();
  uint32_t irq_status = 0;

  while (1) {
    if (!pn5180_read_register(IRQ_STATUS, &irq_status)) {
      LOG_ERR("Failed to read IRQ_STATUS");
      return -EIO;
    }

    if (irq_status & TX_RFON_IRQ_STAT) {
      break; // RF_ON confirmed
    }

    if ((k_uptime_get() - start_time) > 500) {
      LOG_ERR("Timeout waiting for TX_RFON_IRQ_STAT");
      return -ETIMEDOUT;
    }

    k_msleep(5); // small poll delay
  }

  // 3. Clear the RF_ON bit in IRQ_STATUS
  uint32_t reg_value = TX_RFON_IRQ_STAT;
  uint8_t cmd_clear[] = {PN5180_WRITE_REGISTER,    IRQ_CLEAR,
                         (reg_value >> 24) & 0xFF, (reg_value >> 16) & 0xFF,
                         (reg_value >> 8) & 0xFF,  (reg_value & 0xFF)};

  if (!pn5180_spi_send_bytes(cmd_clear, sizeof(cmd_clear))) {
    LOG_ERR("Failed to clear RF_ON IRQ bit");
    return -EIO;
  }

  LOG_DBG("RF activated successfully");
  return 0;
}

int pn5180_disable_rf(void) {
  uint8_t cmd[] = {PN5180_RF_OFF, 0x00};

  // 1. Send RF_OFF command
  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to send RF_OFF command");
    return -EIO;
  }

  // 2. Wait until TX_RFOFF_IRQ_STAT is set (timeout 500 ms)
  int64_t start_time = k_uptime_get();
  uint32_t irq_status = 0;

  while (1) {
    if (!pn5180_read_register(IRQ_STATUS, &irq_status)) {
      LOG_ERR("Failed to read IRQ_STATUS");
      return -EIO;
    }

    if (irq_status & TX_RFOFF_IRQ_STAT) {
      break; // RF_OFF confirmed
    }

    if ((k_uptime_get() - start_time) > 500) {
      LOG_ERR("Timeout waiting for TX_RFOFF_IRQ_STAT");
      return -ETIMEDOUT;
    }

    k_msleep(5); // small poll delay
  }

  // 3. Clear the RF_OFF bit in IRQ_STATUS
  uint32_t reg_value = TX_RFOFF_IRQ_STAT;
  uint8_t *reg_bytes = (uint8_t *)&reg_value;
  uint8_t cmd_clear[] = {PN5180_WRITE_REGISTER, IRQ_CLEAR,    reg_bytes[0],
                         reg_bytes[1],          reg_bytes[2], reg_bytes[3]};

  if (!pn5180_spi_send_bytes(cmd_clear, sizeof(cmd_clear))) {
    LOG_ERR("Failed to clear RF_OFF IRQ bit");
    return -EIO;
  }

  LOG_DBG("RF deactivated successfully");
  return 0;
}

int pn5180_read_eeprom(uint8_t start_addr, uint8_t *buffer, size_t length) {
  if (!buffer || length == 0) {
    return -EINVAL;
  }

  // 1. Build the READ_EEPROM command
  uint8_t cmd[] = {PN5180_READ_EEPROM, start_addr, (uint8_t)length};

  // 2. Send the command
  if (!pn5180_spi_send_bytes(cmd, sizeof(cmd))) {
    LOG_ERR("Failed to send READ_EEPROM command");
    return -EIO;
  }

  // 3. Read the EEPROM data
  if (!pn5180_spi_read_bytes(buffer, length)) {
    LOG_ERR("Failed to read EEPROM data");
    return -EIO;
  }

  LOG_DBG("Read %d EEPROM bytes from 0x%02X", length, start_addr);
  return 0;
}

int pn5180_send_end_of_frame(void) {
  // 1. Configure TX to only send EOF symbol
  uint8_t cmd_cfg[] = {PN5180_WRITE_REGISTER_AND_MASK,
                       TX_CONFIG,
                       0b00111111,
                       0b11111011,
                       0xFF,
                       0xFF};

  if (!pn5180_spi_send_bytes(cmd_cfg, sizeof(cmd_cfg))) {
    LOG_ERR("Failed to configure TX for EOF");
    return -EIO;
  }

  // 2. Trigger SEND_DATA with no payload
  uint8_t cmd_send[] = {PN5180_SEND_DATA, 0x00};

  if (!pn5180_spi_send_bytes(cmd_send, sizeof(cmd_send))) {
    LOG_ERR("Failed to send EOF symbol");
    return -EIO;
  }

  LOG_DBG("EOF symbol sent successfully");
  return 0;
}
bool pn5180_read_reception_buffer(uint8_t *buffer, int16_t len) {
  if (!buffer || len <= 0 || len > MAX_RX_LENGTH) {
    LOG_ERR("Invalid read length request (%d bytes)", len);
    return false;
  }

  // 1. Send READ_DATA command
  uint8_t cmd_read[] = {PN5180_READ_DATA, 0x00};
  if (!pn5180_spi_send_bytes(cmd_read, sizeof(cmd_read))) {
    LOG_ERR("Failed to send READ_DATA command");
    return false;
  }

  // 2. Read reception buffer
  if (!pn5180_spi_read_bytes(buffer, len)) {
    LOG_ERR("Failed to read reception buffer");
    return false;
  }

  LOG_DBG("Read %d bytes from reception buffer", len);
  return true;
}

bool pn5180_get_inventory(uint8_t *uid) {
  if (!uid)
    return false;

  uint8_t buffer[READ_BUFFER_SIZE];
  bool tag_detected = false;

  // 1. Load ISO15693 protocol into RF registers
  pn5180_load_iso15693_config();

  // 2. Switch RF ON
  if (pn5180_activate_rf() != 0)
    return false;

  // 3. Clear IRQ_STATUS
  pn5180_clear_irq();

  // 4. Set IDLE
  pn5180_set_idle();

  // 5. Activate TRANSCEIVE routine
  pn5180_activate_transceive();

  // 6. Send inventory command
  pn5180_send_inventory_cmd();

  // 7. Loop over all 16 time slots
  for (int slot = 0; slot < 16 && !tag_detected; slot++) {
    k_yield(); // prevent watchdog timeout

    // 8. Read RX_STATUS register (4 bytes)
    uint32_t rx_status = 0;
    if (!pn5180_read_register(RX_STATUS, &rx_status)) {
      LOG_ERR("Failed to read RX_STATUS");
      break;
    }

    // Number of bytes received = lower 9 bits
    uint16_t len = (uint16_t)(rx_status & 0x01FF);
    if (len > READ_BUFFER_SIZE)
      len = READ_BUFFER_SIZE;

    if (len > 0) {
      // 9. Read reception buffer
      if (!pn5180_read_reception_buffer(buffer, len)) {
        LOG_ERR("Invalid response");
        continue;
      }

      // 10. First byte = flags, 0x00 = no error
      if (buffer[0] == 0x00) {
        // Reverse the UID bytes to match MSB-first
        uint8_t *raw_uid = &buffer[2];
        for (int i = 0; i < 8; i++) {
          uid[i] = raw_uid[7 - i];
        }
        tag_detected = true;
        break;
      }
      // 11. Error flag set
      else if (buffer[0] & 0x01) {
        uint8_t error_code = buffer[1];
        // pn5180_handle_error((ISO15693ErrorCode)error_code);
      }
    }

    // 12-15. Prepare for next time slot (if any)
    if (slot < 15) {
      pn5180_set_idle();
      pn5180_activate_transceive();
      pn5180_clear_irq();
      pn5180_send_end_of_frame();
    }
  }

  // 16. Switch RF field off
  pn5180_disable_rf();

  return tag_detected;
}