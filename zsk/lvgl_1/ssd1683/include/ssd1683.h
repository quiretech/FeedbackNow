#ifndef SSD1683_H
#define SSD1683_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

// Command definitions (matching reference driver)
#define SSD1683_CMD_SWRESET 0x12
#define SSD1683_CMD_DRIVER_OUTPUT 0x01
#define SSD1683_CMD_DISPLAY_UPDATE 0x21
#define SSD1683_CMD_BORDER_WAVEFORM 0x3C
#define SSD1683_CMD_DATA_ENTRY_MODE 0x11
#define SSD1683_CMD_SET_RAM_X 0x44
#define SSD1683_CMD_SET_RAM_Y 0x45
#define SSD1683_CMD_SET_RAM_X_COUNT 0x4E
#define SSD1683_CMD_SET_RAM_Y_COUNT 0x4F
#define SSD1683_CMD_WRITE_RAM 0x24
#define SSD1683_CMD_WRITE_RAM2 0x26
#define SSD1683_CMD_MASTER_ACTIVATION 0x20
#define SSD1683_CMD_DEEP_SLEEP 0x10
#define SSD1683_CMD_TEMP_WRITE 0x1A
#define SSD1683_CMD_TEMP_LOAD 0x22

struct ssd1683_config {
  struct spi_dt_spec bus; // Modern Zephyr: combines device + config
  struct gpio_dt_spec dc;
  struct gpio_dt_spec rst;
  struct gpio_dt_spec busy;
  uint16_t width;
  uint16_t height;
};

// ============================================================================
// Function Declarations
// ============================================================================

// Low-level SPI functions
void ssd1683_write_cmd(const struct ssd1683_config *cfg, uint8_t cmd);
void ssd1683_write_data(const struct ssd1683_config *cfg, uint8_t data);

// Hardware control functions
void ssd1683_reset(const struct ssd1683_config *cfg);
void ssd1683_deep_sleep(const struct ssd1683_config *cfg);

// Initialization functions
void ssd1683_hw_init(const struct ssd1683_config *cfg);
void ssd1683_hw_init_fast(const struct ssd1683_config *cfg);
void ssd1683_hw_init_partial(const struct ssd1683_config *cfg);

// Update functions
void ssd1683_update(const struct ssd1683_config *cfg);
void ssd1683_update_fast(const struct ssd1683_config *cfg);
void ssd1683_update_partial(const struct ssd1683_config *cfg);

// Display functions
void ssd1683_write_ram_bw(const struct ssd1683_config *cfg, const uint8_t *data,
                          uint16_t length);
void ssd1683_fillwhite(const struct ssd1683_config *cfg);
void ssd1683_fillblack(const struct ssd1683_config *cfg);

// Partial refresh functions
void ssd1683_set_base_map(const struct ssd1683_config *cfg, const uint8_t *data,
                          uint16_t length);
void ssd1683_partial_refresh(const struct ssd1683_config *cfg, uint16_t x_start,
                             uint16_t y_start, const uint8_t *data,
                             uint16_t width, uint16_t height);
void ssd1683_partial_refresh_full(const struct ssd1683_config *cfg,
                                  const uint8_t *data, uint16_t length);

#endif // SSD1683_H
