#include "pn5180.h"
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/posix/sys/stat.h> 


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
static int pn5180_read_register(const struct device *dev, uint8_t reg_addr,
                                uint32_t *reg_value);

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

/*
 * Hard-reset the PN5180 after a BUSY/SPI hang so later commands can recover.
 * Always drives NSS inactive first.
 */
static int pn5180_recover(const struct device *dev) {
  LOG_WRN("PN5180 recovery: hard reset");
  cs_high(dev);
  return pn5180_reset(dev);
}

/* Wait until RX_IRQ_STAT is set, or slot timeout elapses (not an error). */
static int pn5180_wait_rx_irq_slot(const struct device *dev) {
  int64_t deadline = k_uptime_get() + PN5180_SLOT_RX_WAIT_MS;
  uint32_t irq_status = 0;
  int ret;

  while (k_uptime_get() < deadline) {
    ret = pn5180_read_register(dev, IRQ_STATUS, &irq_status);
    if (ret) {
      return ret;
    }
    if (irq_status & RX_IRQ_STAT) {
      return PN5180_OK;
    }
    k_msleep(PN5180_SLOT_POLL_MS);
  }

  return PN5180_OK; /* empty slot is normal during 16-slot inventory */
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

/* NFC Protocol Functions (ISO15693 only) */
static int pn5180_load_iso15693_config(const struct device *dev) {
  uint8_t cmd[] = {PN5180_LOAD_RF_CONFIG, 0x0D, 0x8D};
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
 * Includes CRC and collision checking for data integrity.
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

  /* Read RX_STATUS to get response length and check for errors */
  uint32_t rx_status = 0;
  ret = pn5180_read_register(dev, RX_STATUS, &rx_status);
  if (ret) {
    return ret;
  }

  /* Check for collision */
  if (rx_status & RX_STATUS_COLLISION_DET) {
    LOG_WRN("Collision detected during reception");
    return ISO15693_EC_COLLISION;
  }

  /* Check for protocol error */
  if (rx_status & RX_STATUS_PROTOCOL_ERR) {
    LOG_ERR("Protocol error detected");
    return ISO15693_EC_UNKNOWN_ERROR;
  }

  /* Check for data integrity error (actual CRC failure) */
  if (rx_status & RX_STATUS_DATA_INTEGRITY_ERR) {
    LOG_ERR("Data integrity error (CRC failure) detected");
    return ISO15693_EC_CRC_ERROR;
  }

  uint16_t len = (uint16_t)(rx_status & RX_STATUS_LEN_MASK);
  LOG_DBG("ISO15693 response length: %d bytes, RX_STATUS=0x%08X", len,
          rx_status);

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

  /*
   * Note: RX_STATUS_CRC_OK bit is NOT checked here.
   * For ISO15693, the PN5180 RF configuration handles CRC differently -
   * the CRC_OK bit may not be set even when CRC is valid.
   * Actual CRC failures are caught by RX_STATUS_DATA_INTEGRITY_ERR above.
   */

  /* Check response flags for ISO15693 errors */
  if (response[0] & 0x01) { /* Error flag set */
    uint8_t error_code = response[1];
    LOG_ERR("ISO15693 error response: 0x%02X", error_code);
    if (error_code >= 0xA0 && error_code <= 0xDF) {
      return ISO15693_EC_CUSTOM_CMD_ERROR;
    }
    /* Return the actual ISO15693 error code if it's a standard one */
    if (error_code >= 0x01 && error_code <= 0x14) {
      return error_code; /* Standard ISO15693 error codes */
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

// This was add in ver 1.5.3 but never tested NK, hence commented out for now. It can be re-enabled later if needed.

// /*
//  * Read a multiple blocks from an ISO15693 tag.
//  * uid: 8-byte UID in MSB-first format (as returned by get_inventory)
//  * block_num: Block number to read (0-27 for SLIX with 28 blocks)
//  * block_data: Buffer to receive block data (must be at least block_size bytes)
//  * block_size: Expected block size (4 bytes for SLIX)
//  */
// static int pn5180_read_multiple_blocks(const struct device *dev, const uint8_t *uid, 
//                                         uint8_t start_block, uint8_t num_blocks,
//                                         uint8_t *block_data, size_t data_len, size_t *bytes_read) {

//   size_t block_size = 4; /* SLIX block size */
//   size_t expected_tag_data = num_blocks * block_size; 
//   size_t total_rx_needed = 1 + expected_tag_data;
                                        
//   uint8_t response[128]; /* Response: flags(1) + data(up to 32) */
//   int ret;

  
//   if (total_rx_needed > sizeof(response)) {
//       return -ENOMEM;
//   }

//   /* Build ReadMultipleBlocks command
//    * Format: [flags, cmd, UID(8 bytes LSB first), block_num]
//    */
//   uint8_t cmd[14];
//   cmd[0] = ISO15693_FLAG_HIGH_DATA_RATE | ISO15693_FLAG_ADDRESS; /* 0x22 */
//   cmd[1] = ISO15693_CMD_READ_MULTIPLE_BLOCKS;                       /* 0x23 */

//   /* UID must be sent LSB first, but we store it MSB first */
//   for (int i = 0; i < 8; i++) {
//     cmd[2 + i] = uid[7 - i];
//   }
 
//   cmd[12] = start_block; /* Start block */ 
//   cmd[13] = num_blocks -1; /* Number of blocks to read minus 1 */ 

//   LOG_DBG("ReadMultipleBlocks: start_block=%d num_blocks=%d", start_block, num_blocks);
  

//   ret = pn5180_issue_iso15693_command(dev, cmd, sizeof(cmd), response,
//                                       sizeof(response));
//   if (ret < 0) {
//     return ret;
//   }

//   uint8_t iso_response_flag = response[0];
//   if (iso_response_flag & 0x01) {
//       printk("pn5180: Multi-block read rejected. Code: 0x%02X\n", response[1]);
//       return -EIO;
//   }

//   // Extract the flat block data stream
//   if (expected_tag_data > data_len) {
//       expected_tag_data = data_len; // Prevent buffer overflows
//   }

//   /* Copy block data (skip flags byte) */
//   memcpy(block_data, &response[1], expected_tag_data);
//   *bytes_read = expected_tag_data;

//   LOG_DBG("ReadMultipleBlocks: start_block=%d num_blocks=%d bytes_read=%d", start_block, num_blocks, expected_tag_data);

//   return PN5180_OK;
// }

/*
 * Write a single block to an ISO15693 tag.
 * uid: 8-byte UID in MSB-first format (as returned by get_inventory)
 * block_num: Block number to write (0-27 for SLIX with 28 blocks)
 * block_data: Data to write (must be block_size bytes)
 * block_size: Block size (4 bytes for SLIX)
 */
static int pn5180_write_single_block(const struct device *dev,
                                     const uint8_t *uid, uint8_t block_num,
                                     const uint8_t *block_data,
                                     size_t block_size) {
  uint8_t response[8]; /* Response: flags(1) + optional error code */
  int ret;

  if (block_size > ISO15693_MAX_BLOCK_SIZE) {
    LOG_ERR("Block size too large: %d", block_size);
    return PN5180_ERR_INVALID_PARAM;
  }

  /* Build WriteSingleBlock command
   * Format: [flags, cmd, UID(8 bytes LSB first), block_num, data...]
   */
  uint8_t cmd[11 + ISO15693_MAX_BLOCK_SIZE];
  cmd[0] = ISO15693_FLAG_HIGH_DATA_RATE | ISO15693_FLAG_ADDRESS; /* 0x22 */
  cmd[1] = ISO15693_CMD_WRITE_SINGLE_BLOCK;                      /* 0x21 */

  /* UID must be sent LSB first, but we store it MSB first */
  for (int i = 0; i < 8; i++) {
    cmd[2 + i] = uid[7 - i];
  }
  cmd[10] = block_num;

  /* Copy block data */
  memcpy(&cmd[11], block_data, block_size);

  LOG_DBG("WriteSingleBlock: block=%d, size=%d", block_num, block_size);

  ret = pn5180_issue_iso15693_command(dev, cmd, 11 + block_size, response,
                                      sizeof(response));
  if (ret < 0) {
    return ret;
  }

  /* Write command returns just flags byte on success */
  LOG_DBG("Block %d written successfully", block_num);

  return PN5180_OK;
}

/*
 * Get System Information from an ISO15693 tag.
 * Returns tag parameters including UID, block size, number of blocks, etc.
 */
static int pn5180_get_system_info_internal(const struct device *dev,
                                           const uint8_t *uid,
                                           struct iso15693_system_info *info) {
  uint8_t response[32]; /* Max response size */
  int ret;

  /* Build GetSystemInfo command
   * Format: [flags, cmd, UID(8 bytes LSB first)]
   */
  uint8_t cmd[10];
  cmd[0] = ISO15693_FLAG_HIGH_DATA_RATE | ISO15693_FLAG_ADDRESS; /* 0x22 */
  cmd[1] = ISO15693_CMD_GET_SYSTEM_INFO;                         /* 0x2B */

  /* UID must be sent LSB first, but we store it MSB first */
  for (int i = 0; i < 8; i++) {
    cmd[2 + i] = uid[7 - i];
  }

  LOG_DBG("GetSystemInfo for UID");

  ret = pn5180_issue_iso15693_command(dev, cmd, sizeof(cmd), response,
                                      sizeof(response));
  if (ret < 0) {
    return ret;
  }

  /* Minimum response: flags(1) + info_flags(1) + UID(8) = 10 bytes */
  if (ret < 10) {
    LOG_ERR("GetSystemInfo response too short: %d", ret);
    return ISO15693_EC_INVALID_RESPONSE;
  }

  /* Clear info struct */
  memset(info, 0, sizeof(*info));

  /* Parse response */
  uint8_t *p = &response[1]; /* Skip response flags */
  info->info_flags = *p++;

  /* UID (LSB first in response, convert to MSB first) */
  for (int i = 0; i < 8; i++) {
    info->uid[i] = p[7 - i];
  }
  p += 8;

  /* Optional DSFID field */
  if (info->info_flags & ISO15693_INFO_FLAG_DSFID) {
    info->dsfid = *p++;
    LOG_DBG("DSFID: 0x%02X", info->dsfid);
  }

  /* Optional AFI field */
  if (info->info_flags & ISO15693_INFO_FLAG_AFI) {
    info->afi = *p++;
    LOG_DBG("AFI: 0x%02X", info->afi);
  }

  /* Optional VICC memory size */
  if (info->info_flags & ISO15693_INFO_FLAG_VICC_MEM) {
    info->num_blocks = *p++ + 1; /* Number of blocks (0 means 1 block) */
    uint8_t block_info = *p++;
    info->block_size = (block_info & 0x1F) + 1; /* Block size in bytes */
    LOG_DBG("Memory: %d blocks x %d bytes = %d bytes total", info->num_blocks,
            info->block_size, info->num_blocks * info->block_size);
  }

  /* Optional IC reference */
  if (info->info_flags & ISO15693_INFO_FLAG_IC_REF) {
    info->ic_reference = *p++;
    LOG_DBG("IC Reference: 0x%02X", info->ic_reference);
  }

  return PN5180_OK;
}

/*
 * Read data from PN5180 EEPROM.
 * addr: EEPROM address to read from
 * data: Buffer to store read data
 * len: Number of bytes to read (max 255)
 */
static int pn5180_read_eeprom_internal(const struct device *dev, uint8_t addr,
                                       uint8_t *data, size_t len) {
  int ret;

  if (!data || len == 0 || len > 255) {
    return PN5180_ERR_INVALID_PARAM;
  }

  /* Build READ_EEPROM command: [cmd, addr, len] */
  uint8_t cmd[] = {PN5180_READ_EEPROM, addr, (uint8_t)len};

  ret = pn5180_spi_send_bytes(dev, cmd, sizeof(cmd));
  if (ret) {
    LOG_ERR("Failed to send READ_EEPROM command");
    return ret;
  }

  ret = pn5180_spi_read_bytes(dev, data, len);
  if (ret) {
    LOG_ERR("Failed to read EEPROM data");
    return ret;
  }

  LOG_DBG("Read %d bytes from EEPROM addr 0x%02X", len, addr);
  return PN5180_OK;
}

/*
 * Get PN5180 firmware and product version information.
 */
static int pn5180_get_version_internal(const struct device *dev,
                                       struct pn5180_version_info *info) {
  uint8_t data[6];
  int ret;

  if (!info) {
    return PN5180_ERR_INVALID_PARAM;
  }

  /* Read product version (2 bytes at 0x10) */
  ret =
      pn5180_read_eeprom_internal(dev, PN5180_EEPROM_PRODUCT_VERSION, data, 2);
  if (ret) {
    LOG_ERR("Failed to read product version");
    return ret;
  }
  info->product_version = sys_get_le16(data);

  /* Read firmware version (2 bytes at 0x12) */
  ret =
      pn5180_read_eeprom_internal(dev, PN5180_EEPROM_FIRMWARE_VERSION, data, 2);
  if (ret) {
    LOG_ERR("Failed to read firmware version");
    return ret;
  }
  info->firmware_version = sys_get_le16(data);

  /* Read EEPROM version (2 bytes at 0x14) */
  ret = pn5180_read_eeprom_internal(dev, PN5180_EEPROM_EEPROM_VERSION, data, 2);
  if (ret) {
    LOG_ERR("Failed to read EEPROM version");
    return ret;
  }
  info->eeprom_version = sys_get_le16(data);

  LOG_INF("PN5180 Product: %d.%d, Firmware: %d.%d, EEPROM: %d.%d",
          (info->product_version >> 8) & 0xFF, info->product_version & 0xFF,
          (info->firmware_version >> 8) & 0xFF, info->firmware_version & 0xFF,
          (info->eeprom_version >> 8) & 0xFF, info->eeprom_version & 0xFF);

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

/*
 * Get human-readable error string for ISO15693/PN5180 error codes.
 */
const char *pn5180_strerror(int error_code) {
  switch (error_code) {
  /* Success */
  case PN5180_OK:
    return "OK";

  /* Driver-specific errors (negative) */
  case PN5180_ERR_TIMEOUT:
    return "Timeout";
  case PN5180_ERR_SPI:
    return "SPI communication error";
  case PN5180_ERR_GPIO:
    return "GPIO error";
  case PN5180_ERR_INVALID_PARAM:
    return "Invalid parameter";

  /* ISO15693 driver errors (negative) */
  case ISO15693_EC_NO_CARD:
    return "No card detected";
  case ISO15693_EC_COLLISION:
    return "Collision detected";
  case ISO15693_EC_TIMEOUT:
    return "Response timeout";
  case ISO15693_EC_CRC_ERROR:
    return "CRC error";
  case ISO15693_EC_INVALID_RESPONSE:
    return "Invalid response format";

  /* ISO15693 standard errors (positive, from tag) */
  case ISO15693_EC_NOT_SUPPORTED:
    return "Command not supported";
  case ISO15693_EC_NOT_RECOGNIZED:
    return "Command not recognized (format error)";
  case ISO15693_EC_OPTION_NOT_SUPPORTED:
    return "Option not supported";
  case ISO15693_EC_UNKNOWN_ERROR:
    return "Unknown error";
  case ISO15693_EC_BLOCK_NOT_AVAILABLE:
    return "Block not available";
  case ISO15693_EC_BLOCK_ALREADY_LOCKED:
    return "Block already locked";
  case ISO15693_EC_BLOCK_IS_LOCKED:
    return "Block is locked and cannot be changed";
  case ISO15693_EC_BLOCK_NOT_PROGRAMMED:
    return "Block programming failed";
  case ISO15693_EC_BLOCK_NOT_LOCKED:
    return "Block lock failed";

  default:
    /* Check for custom command error range (0xA0-0xDF) */
    if (error_code >= 0xA0 && error_code <= 0xDF) {
      return "Custom command error";
    }
    return "Unknown error code";
  }
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
  default:
    LOG_ERR("Unsupported protocol: %d (only ISO15693 supported)", protocol);
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
  bool need_recover = false;
  int ret;

  if (!data->initialized || !uid || uid_len < 8) {
    return PN5180_ERR_INVALID_PARAM;
  }

  k_mutex_lock(&data->mutex, K_FOREVER);

  /* Load protocol configuration */
  ret = pn5180_load_iso15693_config(dev);
  if (ret) {
    need_recover = true;
    goto inventory_done;
  }

  /* Activate RF */
  ret = pn5180_activate_rf(dev);
  if (ret) {
    need_recover = true;
    goto inventory_done;
  }

  /* Let the RF field stabilize before inventory (matches working rwPi path) */
  k_msleep(PN5180_RF_SETTLE_MS);

  /* Clear IRQ and set up for inventory */
  ret = pn5180_clear_irq(dev);
  if (ret) {
    need_recover = true;
    goto inventory_done;
  }
  ret = pn5180_set_idle(dev);
  if (ret) {
    need_recover = true;
    goto inventory_done;
  }
  ret = pn5180_activate_transceive(dev);
  if (ret) {
    need_recover = true;
    goto inventory_done;
  }
  ret = pn5180_send_inventory_cmd(dev);
  if (ret) {
    need_recover = true;
    goto inventory_done;
  }

  /* Allow TX to finish and tags to process the inventory request */
  k_msleep(PN5180_INVENTORY_TX_WAIT_MS);

  /* Loop over 16 ISO15693 slots */
  for (int slot = 0; slot < 16 && !tag_detected; slot++) {
    /* Wait for RX complete before touching RX_STATUS / READ_DATA */
    ret = pn5180_wait_rx_irq_slot(dev);
    if (ret) {
      need_recover = true;
      break;
    }

    uint32_t rx_status = 0;
    ret = pn5180_read_register(dev, RX_STATUS, &rx_status);
    if (ret) {
      need_recover = true;
      break;
    }

    uint16_t len = (uint16_t)(rx_status & 0x01FF);
    if (len > PN5180_READ_BUFFER_SIZE) {
      len = PN5180_READ_BUFFER_SIZE;
    }

    if (len > 0) {
      ret = pn5180_read_reception_buffer(dev, buffer, len);
      if (ret) {
        /* Do not keep polling while BUSY is stuck — recover instead */
        need_recover = true;
        break;
      }

      if (len >= 10 && buffer[0] == 0x00) {
        /* Copy UID (reverse bytes for MSB-first) */
        uint8_t *raw_uid = &buffer[2];
        for (int i = 0; i < 8 && i < (int)uid_len; i++) {
          uid[i] = raw_uid[7 - i];
        }
        tag_detected = true;
        break;
      }

      /* Clear RX_IRQ after consuming a non-match response */
      pn5180_clear_irq(dev);
    }

    /* Prepare for next slot */
    if (slot < 15 && !tag_detected) {
      ret = pn5180_set_idle(dev);
      if (ret) {
        need_recover = true;
        break;
      }
      ret = pn5180_activate_transceive(dev);
      if (ret) {
        need_recover = true;
        break;
      }
      ret = pn5180_clear_irq(dev);
      if (ret) {
        need_recover = true;
        break;
      }
      ret = pn5180_send_end_of_frame(dev);
      if (ret) {
        need_recover = true;
        break;
      }
    }
  }

inventory_done:
  if (need_recover) {
    pn5180_recover(dev);
  } else {
    /* Disable RF after a normal inventory cycle */
    int rf_ret = pn5180_disable_rf(dev);
    if (rf_ret) {
      LOG_WRN("RF_OFF failed after inventory (%d), recovering", rf_ret);
      pn5180_recover(dev);
      if (!ret) {
        ret = rf_ret;
      }
    }
  }

  k_mutex_unlock(&data->mutex);

  if (need_recover && ret) {
    return ret;
  }
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
    LOG_ERR("Buffer too small: need %d bytes, got %d", num_blocks * block_size,
            data_len);
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
    ret = pn5180_read_single_block(dev, uid, block,
                                   &tag_data[block * block_size], block_size);
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

/*
 * Write a single block to the tag (public API).
 */
static int pn5180_driver_write_block(const struct device *dev,
                                     const uint8_t *uid, uint8_t block_num,
                                     const uint8_t *block_data,
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

  /* Write the block */
  ret = pn5180_write_single_block(dev, uid, block_num, block_data, block_size);

  /* Disable RF */
  pn5180_disable_rf(dev);

  k_mutex_unlock(&data->mutex);
  return ret;
}

/*
 * Get system information from an ISO15693 tag (public API).
 */
static int pn5180_driver_get_system_info(const struct device *dev,
                                         const uint8_t *uid,
                                         struct iso15693_system_info *info) {
  struct pn5180_data *data = dev->data;
  int ret;

  if (!data->initialized || !uid || !info) {
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

  /* Get system info */
  ret = pn5180_get_system_info_internal(dev, uid, info);

  /* Disable RF */
  pn5180_disable_rf(dev);

  k_mutex_unlock(&data->mutex);
  return ret;
}

/*
 * Get PN5180 version information (public API).
 */
static int pn5180_driver_get_version(const struct device *dev,
                                     struct pn5180_version_info *info) {
  struct pn5180_data *data = dev->data;
  int ret;

  if (!data->initialized || !info) {
    return PN5180_ERR_INVALID_PARAM;
  }

  k_mutex_lock(&data->mutex, K_FOREVER);

  ret = pn5180_get_version_internal(dev, info);

  k_mutex_unlock(&data->mutex);
  return ret;
}

/*
 * Read from PN5180 EEPROM (public API).
 */
static int pn5180_driver_read_eeprom(const struct device *dev, uint8_t addr,
                                     uint8_t *eeprom_data, size_t len) {
  struct pn5180_data *data = dev->data;
  int ret;

  if (!data->initialized || !eeprom_data || len == 0) {
    return PN5180_ERR_INVALID_PARAM;
  }

  k_mutex_lock(&data->mutex, K_FOREVER);

  ret = pn5180_read_eeprom_internal(dev, addr, eeprom_data, len);

  k_mutex_unlock(&data->mutex);
  return ret;
}

static int pn5180_driver_write_tag(const struct device *dev,
                                   const uint8_t *write_data, size_t data_len) {
  /* TODO: Implement full tag writing */
  return -ENOTSUP;
}

/*
 * Prepare PN5180 for power off.
 * Ensures clean state before external power is cut.
 * This is a "best effort" shutdown - it won't fail even if RF is already off.
 */
static int pn5180_driver_prepare_poweroff(const struct device *dev) {
  struct pn5180_data *data = dev->data;
  int ret;

  if (!data->initialized) {
    return PN5180_ERR_INVALID_PARAM;
  }

  k_mutex_lock(&data->mutex, K_FOREVER);

  LOG_DBG("Preparing PN5180 for power off");

  /*
   * Step 1: Send RF_OFF command (don't wait for IRQ confirmation)
   * RF might already be off, so we just send the command without waiting.
   * This avoids timeout errors when RF is already disabled.
   */
  uint8_t cmd_rf_off[] = {PN5180_RF_OFF, 0x00};
  ret = pn5180_spi_send_bytes(dev, cmd_rf_off, sizeof(cmd_rf_off));
  if (ret) {
    LOG_WRN("RF_OFF command failed: %d (continuing)", ret);
  }

  /* Small delay to let RF_OFF take effect if it was on */
  k_msleep(5);

  /* Step 2: Clear all IRQ flags */
  ret = pn5180_clear_irq(dev);
  if (ret) {
    LOG_WRN("Failed to clear IRQ flags: %d", ret);
    /* Continue anyway - not critical */
  }

  /* Step 3: Set device to idle mode */
  ret = pn5180_set_idle(dev);
  if (ret) {
    LOG_WRN("Failed to set idle mode: %d", ret);
    /* Continue anyway - not critical */
  }

  LOG_INF("PN5180 ready for power off");

  k_mutex_unlock(&data->mutex);
  return PN5180_OK;
}

/* Driver API Structure */
static const struct pn5180_driver_api pn5180_api = {
    .init = pn5180_driver_init,
    .configure = pn5180_driver_configure,
    .get_inventory = pn5180_driver_get_inventory,
    .read_tag = pn5180_driver_read_tag,
    .read_block = pn5180_driver_read_block,
    .write_block = pn5180_driver_write_block,
    .write_tag = pn5180_driver_write_tag,
    .get_system_info = pn5180_driver_get_system_info,
    .get_version = pn5180_driver_get_version,
    .read_eeprom = pn5180_driver_read_eeprom,
    .prepare_poweroff = pn5180_driver_prepare_poweroff,
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