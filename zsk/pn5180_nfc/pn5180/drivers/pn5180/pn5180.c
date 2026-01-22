#include "pn5180.h"
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#define DT_DRV_COMPAT nxp_pn5180

LOG_MODULE_REGISTER(pn5180, CONFIG_PN5180_LOG_LEVEL);

/* Driver Data Structure */
struct pn5180_data {
  struct k_mutex mutex;
  bool initialized;
  enum pn5180_protocol current_protocol;
  uint32_t timeout_ms;
};

/* Driver Configuration Structure */
struct pn5180_config {
  struct spi_dt_spec spi;
  struct gpio_dt_spec irq_gpio;
  struct gpio_dt_spec rst_gpio;
  struct gpio_dt_spec busy_gpio;
  struct gpio_dt_spec nss_gpio;
  uint32_t timeout_ms;
};

/* Forward declarations */
static int pn5180_read_reception_buffer(const struct device *dev,
                                        uint8_t *buffer, int16_t len);

/* Helper Functions */
static inline void cs_low(const struct device *dev) {
  const struct pn5180_config *config = dev->config;
  gpio_pin_set_dt(&config->nss_gpio, 1);
}

static inline void cs_high(const struct device *dev) {
  const struct pn5180_config *config = dev->config;
  gpio_pin_set_dt(&config->nss_gpio, 0);
}

static int pn5180_reset(const struct device *dev) {
  const struct pn5180_config *config = dev->config;

  gpio_pin_set_dt(&config->rst_gpio, 0);
  k_msleep(PN5180_RESET_DELAY_MS);
  gpio_pin_set_dt(&config->rst_gpio, 1);
  k_msleep(PN5180_POST_RESET_DELAY_MS);

  return PN5180_OK;
}

static bool wait_until_available(const struct device *dev,
                                 k_timeout_t timeout) {
  const struct pn5180_config *config = dev->config;
  int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);

  while (gpio_pin_get_dt(&config->busy_gpio)) {
    if (k_uptime_get() > deadline) {
      LOG_ERR("Timeout: PN5180 still busy");
      return false;
    }
    k_usleep(PN5180_POLL_INTERVAL_US);
  }
  return true;
}

static bool wait_until_busy(const struct device *dev, k_timeout_t timeout) {
  const struct pn5180_config *config = dev->config;
  int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);

  while (!gpio_pin_get_dt(&config->busy_gpio)) {
    if (k_uptime_get() > deadline) {
      LOG_ERR("Timeout: PN5180 never went busy");
      return false;
    }
    k_usleep(PN5180_POLL_INTERVAL_US);
  }
  return true;
}

/* SPI Communication Functions */
static int pn5180_spi_send_bytes(const struct device *dev,
                                 const uint8_t *send_buf, size_t send_len) {
  const struct pn5180_config *config = dev->config;
  int ret;

  if (!wait_until_available(dev, K_MSEC(50))) {
    cs_high(dev);
    return PN5180_ERR_TIMEOUT;
  }

  cs_low(dev);

  struct spi_buf tx_buf = {.buf = (uint8_t *)send_buf, .len = send_len};
  struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};

  ret = spi_write_dt(&config->spi, &tx_set);
  if (ret) {
    LOG_ERR("spi_write failed: %d", ret);
    return PN5180_ERR_SPI;
  }

  if (!wait_until_busy(dev, K_MSEC(50))) {
    cs_high(dev);
    return PN5180_ERR_TIMEOUT;
  }

  cs_high(dev);
  k_msleep(PN5180_CS_GUARD_DELAY_MS);

  if (!wait_until_available(dev, K_MSEC(50))) {
    cs_high(dev);
    return PN5180_ERR_TIMEOUT;
  }
  cs_high(dev);

  return PN5180_OK;
}

static int pn5180_spi_read_bytes(const struct device *dev, uint8_t *recv_buf,
                                 size_t recv_len) {
  const struct pn5180_config *config = dev->config;
  int ret;

  memset(recv_buf, 0x00, recv_len);

  if (!wait_until_available(dev, K_MSEC(50))) {
    cs_high(dev);
    return PN5180_ERR_TIMEOUT;
  }

  cs_low(dev);

  struct spi_buf rx_buf = {.buf = recv_buf, .len = recv_len};
  struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

  ret = spi_read_dt(&config->spi, &rx_set);
  if (ret) {
    LOG_ERR("SPI read failed: %d", ret);
    return PN5180_ERR_SPI;
  }

  if (!wait_until_busy(dev, K_MSEC(50))) {
    cs_high(dev);
    return PN5180_ERR_TIMEOUT;
  }

  cs_high(dev);
  k_msleep(PN5180_CS_GUARD_DELAY_MS);

  if (!wait_until_available(dev, K_MSEC(50))) {
    cs_high(dev);
    return PN5180_ERR_TIMEOUT;
  }
  cs_high(dev);

  return PN5180_OK;
}

/* Register Functions */
static int pn5180_read_register(const struct device *dev, uint8_t reg_addr,
                                uint32_t *reg_value) {
  uint8_t cmd[] = {PN5180_READ_REGISTER, reg_addr};
  uint8_t buf[4];
  int ret;

  ret = pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
  if (ret) {
    LOG_ERR("Failed to send READ_REGISTER command");
    return ret;
  }

  ret = pn5180_spi_read_bytes(dev, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("Failed to read register value");
    return ret;
  }

  *reg_value = sys_get_le32(buf);
  LOG_DBG("Read reg 0x%02X = 0x%08X", reg_addr, *reg_value);
  return PN5180_OK;
}

/* NFC Protocol Functions */
static int pn5180_load_iso15693_config(const struct device *dev) {
  uint8_t cmd[] = {PN5180_LOAD_RF_CONFIG, 0x0D, 0x8D};
  return pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
}

static int pn5180_load_iso14443a_config(const struct device *dev) {
  uint8_t cmd[] = {PN5180_LOAD_RF_CONFIG, 0x00, 0x80};
  return pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
}

static int pn5180_load_iso14443b_config(const struct device *dev) {
  uint8_t cmd[] = {PN5180_LOAD_RF_CONFIG, 0x03, 0x83};
  return pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
}

static int pn5180_clear_irq(const struct device *dev) {
  uint8_t cmd[] = {PN5180_WRITE_REGISTER, IRQ_CLEAR, 0xFF, 0xFF, 0x0F, 0x00};
  return pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
}

static int pn5180_set_idle(const struct device *dev) {
  uint8_t cmd[] = {
      PN5180_WRITE_REGISTER_AND_MASK, SYSTEM_CONFIG, 0xF8, 0xFF, 0xFF, 0xFF};
  return pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
}

static int pn5180_activate_transceive(const struct device *dev) {
  uint8_t cmd[] = {
      PN5180_WRITE_REGISTER_OR_MASK, SYSTEM_CONFIG, 0x03, 0x00, 0x00, 0x00};
  return pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
}

static int pn5180_send_inventory_cmd(const struct device *dev) {
  uint8_t cmd[] = {
      PN5180_SEND_DATA,
      0x00, // framing / reserved
      0x06, // ISO15693 command length
      0x01, // Inventory command code
      0x00  // flags / options
  };
  return pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
}

static int pn5180_activate_rf(const struct device *dev) {
  const struct pn5180_data *data = dev->data;
  uint8_t cmd[] = {PN5180_RF_ON, 0x00};
  int ret;

  ret = pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
  if (ret) {
    return ret;
  }

  int64_t start_time = k_uptime_get();
  uint32_t irq_status = 0;

  while (1) {
    ret = pn5180_read_register(dev, IRQ_STATUS, &irq_status);
    if (ret) {
      return ret;
    }

    if (irq_status & TX_RFON_IRQ_STAT) {
      break;
    }

    if ((k_uptime_get() - start_time) > data->timeout_ms) {
      LOG_ERR("Timeout waiting for TX_RFON_IRQ_STAT");
      return PN5180_ERR_TIMEOUT;
    }

    k_msleep(5);
  }

  uint32_t reg_value = TX_RFON_IRQ_STAT;
  uint8_t cmd_clear[] = {PN5180_WRITE_REGISTER,    IRQ_CLEAR,
                         (reg_value >> 24) & 0xFF, (reg_value >> 16) & 0xFF,
                         (reg_value >> 8) & 0xFF,  (reg_value & 0xFF)};

  return pn5180_spi_send_bytes(dev, cmd_clear, sizeof(cmd_clear));
}

static int pn5180_disable_rf(const struct device *dev) {
  const struct pn5180_data *data = dev->data;
  uint8_t cmd[] = {PN5180_RF_OFF, 0x00};
  int ret;

  ret = pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
  if (ret) {
    return ret;
  }

  int64_t start_time = k_uptime_get();
  uint32_t irq_status = 0;

  while (1) {
    ret = pn5180_read_register(dev, IRQ_STATUS, &irq_status);
    if (ret) {
      return ret;
    }

    if (irq_status & TX_RFOFF_IRQ_STAT) {
      break;
    }

    if ((k_uptime_get() - start_time) > data->timeout_ms) {
      LOG_ERR("Timeout waiting for TX_RFOFF_IRQ_STAT");
      return PN5180_ERR_TIMEOUT;
    }

    k_msleep(5);
  }

  uint32_t reg_value = TX_RFOFF_IRQ_STAT;
  uint8_t *reg_bytes = (uint8_t *)&reg_value;
  uint8_t cmd_clear[] = {PN5180_WRITE_REGISTER, IRQ_CLEAR,    reg_bytes[0],
                         reg_bytes[1],          reg_bytes[2], reg_bytes[3]};

  return pn5180_spi_send_bytes(dev, cmd_clear, sizeof(cmd_clear));
}

static int pn5180_send_end_of_frame(const struct device *dev) {
  uint8_t cmd_cfg[] = {PN5180_WRITE_REGISTER_AND_MASK,
                       TX_CONFIG,
                       0b00111111,
                       0b11111011,
                       0xFF,
                       0xFF};
  int ret;

  ret = pn5180_spi_send_bytes(dev, cmd_cfg, sizeof(cmd_cfg));
  if (ret) {
    return ret;
  }

  uint8_t cmd_send[] = {PN5180_SEND_DATA, 0x00};
  return pn5180_spi_send_bytes(dev, cmd_send, sizeof(cmd_send));
}

/*
 * Issue an ISO15693 command and wait for response.
 * Returns the number of bytes received, or negative error code.
 */
static int pn5180_issue_iso15693_command(const struct device *dev,
                                         const uint8_t *iso_cmd,
                                         size_t iso_cmd_len, uint8_t *response,
                                         size_t response_max_len) {
  const struct pn5180_data *data = dev->data;
  int ret;

  /* Build SEND_DATA command: [0x09, 0x00, iso_cmd...] */
  uint8_t send_cmd[2 + 32]; /* Max ISO command size */
  if (iso_cmd_len > 30) {
    LOG_ERR("ISO15693 command too long: %d", iso_cmd_len);
    return PN5180_ERR_INVALID_PARAM;
  }

  send_cmd[0] = PN5180_SEND_DATA;
  send_cmd[1] = 0x00; /* Framing byte */
  memcpy(&send_cmd[2], iso_cmd, iso_cmd_len);

  /* Clear IRQ, set idle, then activate transceive */
  pn5180_clear_irq(dev);
  pn5180_set_idle(dev);
  pn5180_activate_transceive(dev);

  /* Send the command */
  ret = pn5180_spi_send_bytes(dev, send_cmd, 2 + iso_cmd_len);
  if (ret) {
    LOG_ERR("Failed to send ISO15693 command");
    return ret;
  }

  /* Wait for response */
  k_msleep(10);

  /* Poll for RX_IRQ_STAT indicating reception complete */
  int64_t start_time = k_uptime_get();
  uint32_t irq_status = 0;

  while (1) {
    ret = pn5180_read_register(dev, IRQ_STATUS, &irq_status);
    if (ret) {
      return ret;
    }

    /* Check if we received start of frame (card present) */
    if (irq_status & RX_IRQ_STAT) {
      break; /* Reception complete */
    }

    if ((k_uptime_get() - start_time) > data->timeout_ms) {
      LOG_DBG("Timeout waiting for ISO15693 response (IRQ=0x%08X)", irq_status);
      return ISO15693_EC_NO_CARD;
    }

    k_msleep(5);
  }

  /* Read RX_STATUS to get response length */
  uint32_t rx_status = 0;
  ret = pn5180_read_register(dev, RX_STATUS, &rx_status);
  if (ret) {
    return ret;
  }

  uint16_t len = (uint16_t)(rx_status & 0x01FF);
  LOG_DBG("ISO15693 response length: %d bytes", len);

  if (len == 0) {
    return ISO15693_EC_NO_CARD;
  }

  if (len > response_max_len) {
    LOG_WRN("Response truncated: %d > %d", len, response_max_len);
    len = response_max_len;
  }

  /* Read the response data */
  ret = pn5180_read_reception_buffer(dev, response, len);
  if (ret) {
    return ret;
  }

  /* Check response flags for errors */
  if (response[0] & 0x01) { /* Error flag set */
    uint8_t error_code = response[1];
    LOG_ERR("ISO15693 error response: 0x%02X", error_code);
    if (error_code == 0x10) {
      return ISO15693_EC_BLOCK_NOT_AVAILABLE;
    }
    return ISO15693_EC_UNKNOWN_ERROR;
  }

  /* Clear IRQ flags */
  pn5180_clear_irq(dev);

  return len;
}

/*
 * Read a single block from an ISO15693 tag.
 * uid: 8-byte UID in MSB-first format (as returned by get_inventory)
 * block_num: Block number to read (0-27 for SLIX with 28 blocks)
 * block_data: Buffer to receive block data (must be at least block_size bytes)
 * block_size: Expected block size (4 bytes for SLIX)
 */
static int pn5180_read_single_block(const struct device *dev,
                                    const uint8_t *uid, uint8_t block_num,
                                    uint8_t *block_data, size_t block_size) {
  uint8_t response[16]; /* Response: flags(1) + data(up to 32) */
  int ret;

  /* Build ReadSingleBlock command
   * Format: [flags, cmd, UID(8 bytes LSB first), block_num]
   */
  uint8_t cmd[11];
  cmd[0] = ISO15693_FLAG_HIGH_DATA_RATE | ISO15693_FLAG_ADDRESS; /* 0x22 */
  cmd[1] = ISO15693_CMD_READ_SINGLE_BLOCK;                       /* 0x20 */

  /* UID must be sent LSB first, but we store it MSB first */
  for (int i = 0; i < 8; i++) {
    cmd[2 + i] = uid[7 - i];
  }
  cmd[10] = block_num;

  LOG_DBG("ReadSingleBlock: block=%d", block_num);

  ret = pn5180_issue_iso15693_command(dev, cmd, sizeof(cmd), response,
                                      sizeof(response));
  if (ret < 0) {
    return ret;
  }

  /* Response format: [flags(1), data(block_size)] */
  if (ret < (int)(1 + block_size)) {
    LOG_ERR("ReadSingleBlock response too short: %d", ret);
    return ISO15693_EC_UNKNOWN_ERROR;
  }

  /* Copy block data (skip flags byte) */
  memcpy(block_data, &response[1], block_size);

  LOG_DBG("Block %d data: %02X %02X %02X %02X", block_num, block_data[0],
          block_data[1], block_data[2], block_data[3]);

  return PN5180_OK;
}

static int pn5180_read_reception_buffer(const struct device *dev,
                                        uint8_t *buffer, int16_t len) {
  uint8_t cmd_read[] = {PN5180_READ_DATA, 0x00};
  int ret;

  if (!buffer || len <= 0 || len > PN5180_MAX_RX_LENGTH) {
    LOG_ERR("Invalid read length request (%d bytes)", len);
    return PN5180_ERR_INVALID_PARAM;
  }

  ret = pn5180_spi_send_bytes(dev, cmd_read, sizeof(cmd_read));
  if (ret) {
    LOG_ERR("Failed to send READ_DATA command");
    return ret;
  }

  ret = pn5180_spi_read_bytes(dev, buffer, len);
  if (ret) {
    LOG_ERR("Failed to read reception buffer");
    return ret;
  }

  LOG_DBG("Read %d bytes from reception buffer", len);
  return PN5180_OK;
}

/* Driver API Implementation */
static int pn5180_driver_init(const struct device *dev) {
  const struct pn5180_config *config = dev->config;
  struct pn5180_data *data = dev->data;
  int ret;

  /* Initialize mutex */
  k_mutex_init(&data->mutex);

  /* Check SPI device */
  if (!spi_is_ready_dt(&config->spi)) {
    LOG_ERR("SPI device not ready");
    return -ENODEV;
  }

  /* Configure GPIO pins */
  if (!gpio_is_ready_dt(&config->nss_gpio)) {
    LOG_ERR("NSS GPIO not ready");
    return -ENODEV;
  }
  ret = gpio_pin_configure_dt(&config->nss_gpio, GPIO_OUTPUT_HIGH);
  if (ret) {
    LOG_ERR("Failed to configure NSS GPIO: %d", ret);
    return ret;
  }

  if (!gpio_is_ready_dt(&config->rst_gpio)) {
    LOG_ERR("RST GPIO not ready");
    return -ENODEV;
  }
  ret = gpio_pin_configure_dt(&config->rst_gpio, GPIO_OUTPUT_HIGH);
  if (ret) {
    LOG_ERR("Failed to configure RST GPIO: %d", ret);
    return ret;
  }

  if (!gpio_is_ready_dt(&config->irq_gpio)) {
    LOG_ERR("IRQ GPIO not ready");
    return -ENODEV;
  }
  ret = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
  if (ret) {
    LOG_ERR("Failed to configure IRQ GPIO: %d", ret);
    return ret;
  }

  if (!gpio_is_ready_dt(&config->busy_gpio)) {
    LOG_ERR("BUSY GPIO not ready");
    return -ENODEV;
  }
  ret = gpio_pin_configure_dt(&config->busy_gpio, GPIO_INPUT);
  if (ret) {
    LOG_ERR("Failed to configure BUSY GPIO: %d", ret);
    return ret;
  }

  /* Reset device */
  ret = pn5180_reset(dev);
  if (ret) {
    LOG_ERR("Failed to reset PN5180: %d", ret);
    return ret;
  }

  data->initialized = true;
  data->timeout_ms = config->timeout_ms;
  data->current_protocol = PN5180_PROTOCOL_ISO15693;

  LOG_INF("PN5180 initialized successfully");
  return PN5180_OK;
}

static int pn5180_driver_configure(const struct device *dev,
                                   enum pn5180_protocol protocol) {
  struct pn5180_data *data = dev->data;
  int ret;

  if (!data->initialized) {
    return -ENODEV;
  }

  k_mutex_lock(&data->mutex, K_FOREVER);

  switch (protocol) {
  case PN5180_PROTOCOL_ISO15693:
    ret = pn5180_load_iso15693_config(dev);
    break;
  case PN5180_PROTOCOL_ISO14443A:
    ret = pn5180_load_iso14443a_config(dev);
    break;
  case PN5180_PROTOCOL_ISO14443B:
    ret = pn5180_load_iso14443b_config(dev);
    break;
  default:
    ret = -ENOTSUP;
    break;
  }

  if (ret == PN5180_OK) {
    data->current_protocol = protocol;
  }

  k_mutex_unlock(&data->mutex);
  return ret;
}
static int pn5180_driver_get_inventory(const struct device *dev, uint8_t *uid,
                                       size_t uid_len) {
  struct pn5180_data *data = dev->data;
  uint8_t buffer[PN5180_READ_BUFFER_SIZE];
  bool tag_detected = false;
  int ret;

  if (!data->initialized || !uid || uid_len < 8) {
    return PN5180_ERR_INVALID_PARAM;
  }

  k_mutex_lock(&data->mutex, K_FOREVER);

  /* Load protocol configuration */
  ret = pn5180_load_iso15693_config(dev);
  if (ret) {
    k_mutex_unlock(&data->mutex);
    return ret;
  }

  /* Activate RF */
  ret = pn5180_activate_rf(dev);
  if (ret) {
    k_mutex_unlock(&data->mutex);
    return ret;
  }

  /* Clear IRQ and set up for inventory */
  pn5180_clear_irq(dev);
  pn5180_set_idle(dev);
  pn5180_activate_transceive(dev);
  pn5180_send_inventory_cmd(dev);

  /* Loop over time slots */
  for (int slot = 0; slot < 16 && !tag_detected; slot++) {
    k_yield();

    uint32_t rx_status = 0;
    ret = pn5180_read_register(dev, RX_STATUS, &rx_status);
    if (ret) {
      break;
    }

    uint16_t len = (uint16_t)(rx_status & 0x01FF);
    if (len > PN5180_READ_BUFFER_SIZE) {
      len = PN5180_READ_BUFFER_SIZE;
    }

    if (len > 0) {
      ret = pn5180_read_reception_buffer(dev, buffer, len);
      if (ret) {
        continue;
      }

      if (buffer[0] == 0x00) {
        /* Copy UID (reverse bytes for MSB-first) */
        uint8_t *raw_uid = &buffer[2];
        for (int i = 0; i < 8 && i < uid_len; i++) {
          uid[i] = raw_uid[7 - i];
        }
        tag_detected = true;
        break;
      }
    }

    /* Prepare for next slot */
    if (slot < 15) {
      pn5180_set_idle(dev);
      pn5180_activate_transceive(dev);
      pn5180_clear_irq(dev);
      pn5180_send_end_of_frame(dev);
    }
  }

  /* Disable RF */
  pn5180_disable_rf(dev);

  k_mutex_unlock(&data->mutex);
  return tag_detected ? PN5180_OK : PN5180_ERR_TIMEOUT;
}

/*
 * Read a single block from the tag (public API).
 */
static int pn5180_driver_read_block(const struct device *dev, uint8_t *uid,
                                    uint8_t block_num, uint8_t *block_data,
                                    size_t block_size) {
  struct pn5180_data *data = dev->data;
  int ret;

  if (!data->initialized || !uid || !block_data || block_size == 0) {
    return PN5180_ERR_INVALID_PARAM;
  }

  k_mutex_lock(&data->mutex, K_FOREVER);

  /* Load protocol configuration */
  ret = pn5180_load_iso15693_config(dev);
  if (ret) {
    k_mutex_unlock(&data->mutex);
    return ret;
  }

  /* Activate RF */
  ret = pn5180_activate_rf(dev);
  if (ret) {
    k_mutex_unlock(&data->mutex);
    return ret;
  }

  /* Read the block */
  ret = pn5180_read_single_block(dev, uid, block_num, block_data, block_size);

  /* Disable RF */
  pn5180_disable_rf(dev);

  k_mutex_unlock(&data->mutex);
  return ret;
}

/*
 * Read all data from an ISO15693 tag.
 * This reads all blocks (assuming SLIX with 28 blocks of 4 bytes = 112 bytes).
 */
static int pn5180_driver_read_tag(const struct device *dev, uint8_t *uid,
                                  uint8_t *tag_data, size_t data_len,
                                  size_t *bytes_read) {
  struct pn5180_data *data = dev->data;
  int ret;

  /* SLIX has 28 blocks of 4 bytes each = 112 bytes */
  const uint8_t num_blocks = 28;
  const uint8_t block_size = ISO15693_SLIX_BLOCK_SIZE;

  if (!data->initialized || !uid || !tag_data) {
    return PN5180_ERR_INVALID_PARAM;
  }

  if (data_len < (num_blocks * block_size)) {
    LOG_ERR("Buffer too small: need %d bytes, got %d",
            num_blocks * block_size, data_len);
    return PN5180_ERR_INVALID_PARAM;
  }

  k_mutex_lock(&data->mutex, K_FOREVER);

  /* Load protocol configuration */
  ret = pn5180_load_iso15693_config(dev);
  if (ret) {
    k_mutex_unlock(&data->mutex);
    return ret;
  }

  /* Activate RF */
  ret = pn5180_activate_rf(dev);
  if (ret) {
    k_mutex_unlock(&data->mutex);
    return ret;
  }

  /* Read all blocks */
  size_t total_bytes = 0;
  for (uint8_t block = 0; block < num_blocks; block++) {
    ret =
        pn5180_read_single_block(dev, uid, block, &tag_data[block * block_size],
                                 block_size);
    if (ret) {
      LOG_ERR("Failed to read block %d: %d", block, ret);
      /* Disable RF and return error */
      pn5180_disable_rf(dev);
      k_mutex_unlock(&data->mutex);
      return ret;
    }
    total_bytes += block_size;
  }

  /* Disable RF */
  pn5180_disable_rf(dev);

  if (bytes_read) {
    *bytes_read = total_bytes;
  }

  LOG_INF("Read %d bytes from tag (%d blocks)", total_bytes, num_blocks);

  k_mutex_unlock(&data->mutex);
  return PN5180_OK;
}

static int pn5180_driver_write_tag(const struct device *dev,
                                   const uint8_t *data, size_t data_len) {
  /* TODO: Implement tag writing */
  return -ENOTSUP;
}

/* Driver API Structure */
static const struct pn5180_driver_api pn5180_api = {
    .init = pn5180_driver_init,
    .configure = pn5180_driver_configure,
    .get_inventory = pn5180_driver_get_inventory,
    .read_tag = pn5180_driver_read_tag,
    .read_block = pn5180_driver_read_block,
    .write_tag = pn5180_driver_write_tag,
};

/* Device Instances */
#define PN5180_DEFINE(inst)                                                    \
  static struct pn5180_data pn5180_data_##inst;                                \
  static const struct pn5180_config pn5180_config_##inst = {                   \
      .spi =                                                                   \
          SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),   \
      .irq_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                      \
      .rst_gpio = GPIO_DT_SPEC_INST_GET(inst, rst_gpios),                      \
      .busy_gpio = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),                    \
      .nss_gpio = GPIO_DT_SPEC_INST_GET(inst, nss_gpios),                      \
      .timeout_ms = DT_INST_PROP_OR(inst, timeout_ms, 1000),                   \
  };                                                                           \
  DEVICE_DT_INST_DEFINE(inst, pn5180_driver_init, NULL, &pn5180_data_##inst,   \
                        &pn5180_config_##inst, POST_KERNEL,                    \
                        CONFIG_PN5180_INIT_PRIORITY, &pn5180_api);

DT_INST_FOREACH_STATUS_OKAY(PN5180_DEFINE)