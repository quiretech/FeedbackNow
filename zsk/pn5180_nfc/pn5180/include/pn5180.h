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

/* ISO15693 Commands */
#define ISO15693_CMD_INVENTORY 0x01
#define ISO15693_CMD_READ_SINGLE_BLOCK 0x20
#define ISO15693_CMD_GET_SYSTEM_INFO 0x2B

/* ISO15693 Request Flags */
#define ISO15693_FLAG_HIGH_DATA_RATE 0x02
#define ISO15693_FLAG_INVENTORY 0x04
#define ISO15693_FLAG_ADDRESS 0x20

/* ISO15693 Error Codes */
#define ISO15693_EC_OK 0
#define ISO15693_EC_NO_CARD -10
#define ISO15693_EC_NOT_SUPPORTED -11
#define ISO15693_EC_BLOCK_NOT_AVAILABLE -12
#define ISO15693_EC_UNKNOWN_ERROR -13

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

/* Error codes */
#define PN5180_OK 0
#define PN5180_ERR_TIMEOUT -1
#define PN5180_ERR_SPI -2
#define PN5180_ERR_GPIO -3
#define PN5180_ERR_INVALID_PARAM -4

/* NFC Protocols */
enum pn5180_protocol {
  PN5180_PROTOCOL_ISO15693,
  PN5180_PROTOCOL_ISO14443A,
  PN5180_PROTOCOL_ISO14443B,
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
  int (*write_tag)(const struct device *dev, const uint8_t *data,
                   size_t data_len);
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

static inline int pn5180_write_tag(const struct device *dev,
                                   const uint8_t *data, size_t data_len) {
  return PN5180_API(dev)->write_tag(dev, data, data_len);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_NFC_PN5180_H_ */