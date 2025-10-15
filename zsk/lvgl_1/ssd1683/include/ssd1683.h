#ifndef SSD1683_H
#define SSD1683_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

// SSD1683 Command Definitions
#define SSD1683_CMD_SOFT_RESET 0x12
#define SSD1683_CMD_DEEP_SLEEP 0x10
#define SSD1683_CMD_DATA_ENTRY_MODE 0x11
#define SSD1683_CMD_DISPLAY_UPDATE_CTRL 0x21
#define SSD1683_CMD_DISPLAY_UPDATE 0x20
#define SSD1683_CMD_DISPLAY_UPDATE_SEQ 0x22
#define SSD1683_CMD_WRITE_RAM_BW 0x24
#define SSD1683_CMD_WRITE_RAM_RED 0x26
#define SSD1683_CMD_WRITE_VCOM 0x2C
#define SSD1683_CMD_WRITE_LUT 0x32
#define SSD1683_CMD_BORDER_WAVEFORM 0x3C
#define SSD1683_CMD_SET_RAM_X_ADDR 0x44
#define SSD1683_CMD_SET_RAM_Y_ADDR 0x45
#define SSD1683_CMD_SET_RAM_X_COUNTER 0x4E
#define SSD1683_CMD_SET_RAM_Y_COUNTER 0x4F
#define SSD1683_CMD_WRITE_TEMP 0x1A

// Color defines (for future use)
#define SSD1683_COLOR_WHITE 0
#define SSD1683_COLOR_BLACK 1
#define SSD1683_COLOR_RED 2

struct ssd1683_config {
  struct spi_dt_spec bus; // Modern Zephyr: combines device + config
  struct gpio_dt_spec dc;
  struct gpio_dt_spec rst;
  struct gpio_dt_spec busy;
  uint16_t width;
  uint16_t height;
};

// === Initialization Functions ===
int ssd1683_init(const struct ssd1683_config *cfg);
int ssd1683_init_fast(const struct ssd1683_config *cfg);

// === Low-level Hardware Control ===
void ssd1683_reset(const struct ssd1683_config *cfg);
void ssd1683_write_cmd(const struct ssd1683_config *cfg, uint8_t cmd);
void ssd1683_write_data(const struct ssd1683_config *cfg, uint8_t data);
void ssd1683_write_cmd_buffer(const struct ssd1683_config *cfg, uint8_t cmd,
                              const uint8_t *data, size_t len);
void ssd1683_deep_sleep(const struct ssd1683_config *cfg);

// === Display Control ===
void ssd1683_clear(const struct ssd1683_config *cfg);
void ssd1683_refresh(const struct ssd1683_config *cfg);
void ssd1683_refresh_fast(const struct ssd1683_config *cfg);
void ssd1683_refresh_partial(const struct ssd1683_config *cfg);

// === Buffer Operations ===
// NOTE: This driver operates in MONOCHROME mode only.
// All functions write to BW RAM only. RED RAM is cleared once during init/clear
// to ensure no red pixels interfere with the black/white display.

// Set base map for partial refresh (writes to both 0x24 and 0x26 RAM)
void ssd1683_set_base_map(const struct ssd1683_config *cfg,
                          uint8_t *image_buffer);

// Flush framebuffer to display and refresh
void ssd1683_flush(const struct ssd1683_config *cfg, uint8_t *image_buffer);

// === Advanced Display Functions ===
// Fast display update (reduced quality, faster refresh)
void ssd1683_display_fast(const struct ssd1683_config *cfg, uint8_t *image);

// Partial screen update
void ssd1683_partial_display(const struct ssd1683_config *cfg, uint16_t x,
                             uint16_t y, uint16_t w, uint16_t l,
                             uint8_t *image);

// Write to display buffer without refreshing
void ssd1683_write_display(const struct ssd1683_config *cfg, uint16_t x,
                           uint16_t y, uint16_t w, uint16_t l, uint8_t *image);

// === Window/Cursor Management (for display wrapper) ===
// Set display window (RAM address range)
void ssd1683_set_window(const struct ssd1683_config *cfg, uint16_t x_start,
                        uint16_t y_start, uint16_t x_end, uint16_t y_end);

// Set cursor position (RAM write pointer)
void ssd1683_set_cursor(const struct ssd1683_config *cfg, uint16_t x,
                        uint16_t y);

// === Drawing Helper Functions ===
// Draw vertical line on buffer (application-level helper)
void ssd1683_draw_vline(uint8_t *image, int x, int y_start, int y_end,
                        int width, int height);

// Draw horizontal line on buffer (application-level helper)
void ssd1683_draw_hline(uint8_t *image, int x_start, int x_end, int y,
                        int width, int height);

#endif // SSD1683_H
