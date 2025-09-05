#ifndef PN5180_H
#define PN5180_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#define PN5180_COMMAND_TIMEOUT_MS 800

// PN5180 Commands
// PN5180 defines
// See https://www.nxp.com/docs/en/data-sheet/PN5180A0XX-C1-C2.pdf
// 11.4.3.3 Host Interface Command List
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
// 11.9.1, Table 73 PN5180 Register Address Overview
#define SYSTEM_CONFIG (0x00)
#define IRQ_ENABLE (0x01)
#define IRQ_STATUS (0x02)
#define IRQ_CLEAR (0x03)
#define RX_STATUS (0x13)
#define TX_WAIT_CONFIG (0x17)
#define TX_CONFIG (0x18)
// 11.9.1, Table 76 IRQ_STATUS Register
#define RX_IRQ_STAT (1 << 0)        // End of RF reception IRQ
#define TX_IRQ_STAT (1 << 1)        // End of RF transmission IRQ
#define IDLE_IRQ_STAT (1 << 2)      // Idle IRQ
#define RFOFF_DET_IRQ_STAT (1 << 6) // RF Field OFF detection IRQ
#define RFON_DET_IRQ_STAT (1 << 7)  // RF Field ON detection IRQ
#define TX_RFOFF_IRQ_STAT (1 << 8)  // RF Field OFF in PCD IRQ
#define TX_RFON_IRQ_STAT (1 << 9)   // RF Field ON in PCD IRQ
// 11.9.1 Table 92 RX_STATUS Register
#define RX_COLL_POS                                                            \
  (1 << 19) // These bits show the bit position of the first detected collision
            // in a received frame (7 bits)
#define RX_COLLISION_DETECTED                                                  \
  (1 << 18) // This flag is set to 1, when a collision has occurred
// 11.9.1 Table 97 TX_CONFIG Register
#define TX_DATA_ENABLE                                                         \
  (1 << 10) // If set to 1, transmission of data is enabled otherwise only
            // symbols are transmitted.
#define MAX_RX_LENGTH 508
// The PN5180 receive buffer can hold a max of 508 bytes
// But we only need to transfer 2 bytes + 8 bytes per tag = 10 bytes for a
// single card
#ifndef READ_BUFFER_SIZE
#define READ_BUFFER_SIZE 10
#endif

struct pn5180_cfg {
  const struct device *spi_dev;
  struct spi_config spi_cfg;
  const struct gpio_dt_spec irq;
  const struct gpio_dt_spec rst;
  const struct gpio_dt_spec busy;
  const struct gpio_dt_spec nss;
};

/* Public function prototypes */
int pn5180_gpio_init(void);
#endif
