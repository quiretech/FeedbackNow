#ifndef ZEPHYR_DRIVERS_NFC_PN5180_H_
#define ZEPHYR_DRIVERS_NFC_PN5180_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PN5180 Commands */
#define PN5180_WRITE_REGISTER (0x00)
#define PN5180_WRITE_REGISTER_OR_MASK (0x01)
#define PN5180_WRITE_REGISTER_AND_MASK (0x02)
#define PN5180_READ_REGISTER (0x04)
#define PN5180_WRITE_EEPROM (0x06)
#define PN5180_READ_EEPROM (0x07)
#define PN5180_SEND_DATA (0x09)
#define PN5180_READ_DATA (0x0A)
#define PN5180_LOAD_RF_CONFIG (0x11)
#define PN5180_RF_ON (0x16)
#define PN5180_RF_OFF (0x17)

/* PN5180 EEPROM Addresses */
#define PN5180_EEPROM_DIE_ID (0x00)          /* 16 bytes - Unique die identifier */
#define PN5180_EEPROM_PRODUCT_VERSION (0x10) /* 2 bytes - Product version */
#define PN5180_EEPROM_FIRMWARE_VERSION (0x12)/* 2 bytes - Firmware version */
#define PN5180_EEPROM_EEPROM_VERSION (0x14)  /* 2 bytes - EEPROM version */

/* PN5180 Registers */
#define SYSTEM_CONFIG (0x00)
#define IRQ_ENABLE (0x01)
#define IRQ_STATUS (0x02)
#define IRQ_CLEAR (0x03)
#define RX_STATUS (0x13)
#define TX_WAIT_CONFIG (0x17)
#define TX_CONFIG (0x18)

/* IRQ Status Bits */
#define RX_IRQ_STAT (1 << 0)
#define TX_IRQ_STAT (1 << 1)
#define IDLE_IRQ_STAT (1 << 2)
#define RFOFF_DET_IRQ_STAT (1 << 6)
#define RFON_DET_IRQ_STAT (1 << 7)
#define TX_RFOFF_IRQ_STAT (1 << 8)
#define TX_RFON_IRQ_STAT (1 << 9)
#define RX_SOF_DET_IRQ_STAT (1 << 14)

/* RX_STATUS Register Bits (0x13) */
#define RX_STATUS_LEN_MASK      (0x000001FF) /* Bits 0-8: Received data length */
#define RX_STATUS_LAST_BITS     (0x00000E00) /* Bits 9-11: Last byte valid bits */
#define RX_STATUS_COLLISION_POS (0x0007F000) /* Bits 12-18: Collision position */
#define RX_STATUS_COLLISION_DET (1 << 18)    /* Bit 18: Collision detected */
#define RX_STATUS_PROTOCOL_ERR  (1 << 19)    /* Bit 19: Protocol error */
#define RX_STATUS_CRC_OK        (1 << 20)    /* Bit 20: CRC check passed */
#define RX_STATUS_DATA_INTEGRITY_ERR (1 << 21) /* Bit 21: Data integrity error */

/* ISO15693 Commands */
#define ISO15693_CMD_INVENTORY 0x01
#define ISO15693_CMD_STAY_QUIET 0x02
#define ISO15693_CMD_READ_SINGLE_BLOCK 0x20
#define ISO15693_CMD_WRITE_SINGLE_BLOCK 0x21
#define ISO15693_CMD_LOCK_BLOCK 0x22
#define ISO15693_CMD_READ_MULTIPLE_BLOCKS 0x23
#define ISO15693_CMD_WRITE_MULTIPLE_BLOCKS 0x24
#define ISO15693_CMD_SELECT 0x25
#define ISO15693_CMD_RESET_TO_READY 0x26
#define ISO15693_CMD_WRITE_AFI 0x27
#define ISO15693_CMD_LOCK_AFI 0x28
#define ISO15693_CMD_WRITE_DSFID 0x29
#define ISO15693_CMD_LOCK_DSFID 0x2A
#define ISO15693_CMD_GET_SYSTEM_INFO 0x2B
#define ISO15693_CMD_GET_BLOCK_SECURITY 0x2C

/* ISO15693 Request Flags */
#define ISO15693_FLAG_SUBCARRIER 0x01       /* 0=single, 1=two subcarriers */
#define ISO15693_FLAG_HIGH_DATA_RATE 0x02   /* 0=low, 1=high data rate */
#define ISO15693_FLAG_INVENTORY 0x04        /* 0=no inventory, 1=inventory */
#define ISO15693_FLAG_PROTOCOL_EXT 0x08     /* Protocol extension */
#define ISO15693_FLAG_SELECT 0x10           /* Select flag (non-inventory) */
#define ISO15693_FLAG_ADDRESS 0x20          /* Address flag (non-inventory) */
#define ISO15693_FLAG_OPTION 0x40           /* Option flag */
#define ISO15693_FLAG_AFI 0x10              /* AFI flag (inventory mode) */
#define ISO15693_FLAG_SLOTS 0x20            /* Slots flag (inventory mode) */

/* ISO15693 Info Flags (from Get System Info response) */
#define ISO15693_INFO_FLAG_DSFID 0x01       /* DSFID field present */
#define ISO15693_INFO_FLAG_AFI 0x02         /* AFI field present */
#define ISO15693_INFO_FLAG_VICC_MEM 0x04    /* VICC memory size present */
#define ISO15693_INFO_FLAG_IC_REF 0x08      /* IC reference present */

/* ISO15693 Error Codes (matching ISO standard) */
enum iso15693_error_code {
    ISO15693_EC_OK = 0,
    ISO15693_EC_NOT_SUPPORTED = 0x01,       /* Command not supported */
    ISO15693_EC_NOT_RECOGNIZED = 0x02,      /* Command format error */
    ISO15693_EC_OPTION_NOT_SUPPORTED = 0x03,/* Option not supported */
    ISO15693_EC_UNKNOWN_ERROR = 0x0F,       /* Unknown error */
    ISO15693_EC_BLOCK_NOT_AVAILABLE = 0x10, /* Block not available */
    ISO15693_EC_BLOCK_ALREADY_LOCKED = 0x11,/* Block already locked */
    ISO15693_EC_BLOCK_IS_LOCKED = 0x12,     /* Block is locked */
    ISO15693_EC_BLOCK_NOT_PROGRAMMED = 0x13,/* Block programming failed */
    ISO15693_EC_BLOCK_NOT_LOCKED = 0x14,    /* Block lock failed */
    ISO15693_EC_CUSTOM_CMD_ERROR = 0xA0,    /* Custom command error (0xA0-0xDF) */
    /* Driver-specific error codes (negative, distinct from PN5180_ERR_* codes) */
    ISO15693_EC_NO_CARD = -10,              /* No card detected */
    ISO15693_EC_COLLISION = -11,            /* Collision detected */
    ISO15693_EC_TIMEOUT = -12,              /* Response timeout */
    ISO15693_EC_CRC_ERROR = -13,            /* CRC check failed */
    ISO15693_EC_INVALID_RESPONSE = -14,     /* Invalid response format */
};

/* ISO15693 Tag Constants */
#define ISO15693_UID_LENGTH 8
#define ISO15693_MAX_BLOCK_SIZE 32
#define ISO15693_SLIX_BLOCK_SIZE 4

/* Constants */
#define PN5180_MAX_RX_LENGTH 508
#define PN5180_READ_BUFFER_SIZE 10
#define PN5180_POLL_INTERVAL_US 50
#define PN5180_RESET_DELAY_MS 10
#define PN5180_POST_RESET_DELAY_MS 50
#define PN5180_CS_GUARD_DELAY_MS 5
#define PN5180_RF_SETTLE_MS 50
#define PN5180_INVENTORY_TX_WAIT_MS 20
#define PN5180_SLOT_RX_WAIT_MS 50
#define PN5180_SLOT_POLL_MS 2

/* Error codes */
#define PN5180_OK 0
#define PN5180_ERR_TIMEOUT -1
#define PN5180_ERR_SPI -2
#define PN5180_ERR_GPIO -3
#define PN5180_ERR_INVALID_PARAM -4

/* NFC Protocols (ISO15693 only) */
enum pn5180_protocol {
  PN5180_PROTOCOL_ISO15693,
};

/* ISO15693 System Information Structure */
struct iso15693_system_info {
  uint8_t uid[8];           /* Tag UID (MSB first) */
  uint8_t dsfid;            /* Data Storage Format ID */
  uint8_t afi;              /* Application Family Identifier */
  uint8_t block_size;       /* Bytes per block (1-32) */
  uint8_t num_blocks;       /* Number of blocks (1-256) */
  uint8_t ic_reference;     /* IC manufacturer reference */
  uint8_t info_flags;       /* Which optional fields are present */
};

/* PN5180 Version Information */
struct pn5180_version_info {
  uint16_t product_version;   /* Product version */
  uint16_t firmware_version;  /* Firmware version (major.minor) */
  uint16_t eeprom_version;    /* EEPROM version */
};

/* Driver API */
struct pn5180_driver_api {
  int (*init)(const struct device *dev);
  int (*configure)(const struct device *dev, enum pn5180_protocol protocol);
  int (*get_inventory)(const struct device *dev, uint8_t *uid, size_t uid_len);
  int (*read_tag)(const struct device *dev, uint8_t *uid, uint8_t *data,
                  size_t data_len, size_t *bytes_read);
  int (*read_block)(const struct device *dev, uint8_t *uid, uint8_t block_num,
                    uint8_t *block_data, size_t block_size);
  // int (*read_multiple_blocks)(const struct device *dev, const uint8_t *uid, uint8_t start_block, uint8_t num_blocks,
  //                            uint8_t *block_data, size_t data_len, size_t *bytes_read);
  int (*write_block)(const struct device *dev, const uint8_t *uid,
                     uint8_t block_num, const uint8_t *block_data,
                     size_t block_size);
  int (*write_tag)(const struct device *dev, const uint8_t *data,
                   size_t data_len);
  int (*get_system_info)(const struct device *dev, const uint8_t *uid,
                         struct iso15693_system_info *info);
  int (*get_version)(const struct device *dev, struct pn5180_version_info *info);
  int (*read_eeprom)(const struct device *dev, uint8_t addr, uint8_t *data,
                     size_t len);
  int (*prepare_poweroff)(const struct device *dev);
};

/* Helper macros */
#define PN5180_API(dev) ((const struct pn5180_driver_api *)(dev)->api)

/* API Functions */
static inline int pn5180_init(const struct device *dev) {
  return PN5180_API(dev)->init(dev);
}

static inline int pn5180_configure(const struct device *dev,
                                   enum pn5180_protocol protocol) {
  return PN5180_API(dev)->configure(dev, protocol);
}

static inline int pn5180_get_inventory(const struct device *dev, uint8_t *uid,
                                       size_t uid_len) {
  return PN5180_API(dev)->get_inventory(dev, uid, uid_len);
}

static inline int pn5180_read_tag(const struct device *dev, uint8_t *uid,
                                  uint8_t *data, size_t data_len,
                                  size_t *bytes_read) {
  return PN5180_API(dev)->read_tag(dev, uid, data, data_len, bytes_read);
}

static inline int pn5180_read_block(const struct device *dev, uint8_t *uid,
                                    uint8_t block_num, uint8_t *block_data,
                                    size_t block_size) {
  return PN5180_API(dev)->read_block(dev, uid, block_num, block_data,
                                     block_size);
}

// static int pn5180_read_multiple_blocks(const struct device *dev,
//                                        const uint8_t *uid, uint8_t start_block, uint8_t num_blocks,
//                                        uint8_t *block_data, size_t data_len, size_t *bytes_read) {
//   return PN5180_API(dev)->read_multiple_blocks(dev, uid, start_block, 
//                                         num_blocks, block_data, data_len, bytes_read);
// }

static inline int pn5180_write_block(const struct device *dev,
                                     const uint8_t *uid, uint8_t block_num,
                                     const uint8_t *block_data,
                                     size_t block_size) {
  return PN5180_API(dev)->write_block(dev, uid, block_num, block_data,
                                      block_size);
}

static inline int pn5180_write_tag(const struct device *dev,
                                   const uint8_t *data, size_t data_len) {
  return PN5180_API(dev)->write_tag(dev, data, data_len);
}

static inline int pn5180_get_system_info(const struct device *dev,
                                         const uint8_t *uid,
                                         struct iso15693_system_info *info) {
  return PN5180_API(dev)->get_system_info(dev, uid, info);
}

static inline int pn5180_get_version(const struct device *dev,
                                     struct pn5180_version_info *info) {
  return PN5180_API(dev)->get_version(dev, info);
}

static inline int pn5180_read_eeprom(const struct device *dev, uint8_t addr,
                                     uint8_t *data, size_t len) {
  return PN5180_API(dev)->read_eeprom(dev, addr, data, len);
}

/**
 * @brief Prepare PN5180 for power off
 *
 * Call this before cutting power to the PN5180 module to ensure a clean state.
 * This function:
 * - Disables the RF field
 * - Clears all pending IRQ flags
 * - Sets the device to idle mode
 *
 * After calling this, it is safe to cut power to the PN5180.
 *
 * @param dev Pointer to the device structure
 * @return 0 on success, negative error code on failure
 */
static inline int pn5180_prepare_poweroff(const struct device *dev) {
  return PN5180_API(dev)->prepare_poweroff(dev);
}

/**
 * @brief Get human-readable error string for ISO15693 error codes
 * @param error_code The error code to convert
 * @return Pointer to static string describing the error
 */
const char *pn5180_strerror(int error_code);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_NFC_PN5180_H_ */