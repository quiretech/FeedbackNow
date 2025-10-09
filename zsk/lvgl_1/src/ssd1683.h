#ifndef SSD1683_H
#define SSD1683_H

// #include "image_bitmap.h" // Contains bw_bitmap[15000] - file deleted
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

// Color defines
#define SSD1683_COLOR_WHITE 0
#define SSD1683_COLOR_BLACK 1
#define SSD1683_COLOR_RED 2

struct ssd1683_config {
  const struct device *spi_dev;
  struct spi_config spi_cfg;
  const struct gpio_dt_spec dc;
  const struct gpio_dt_spec rst;
  const struct gpio_dt_spec busy;
  uint16_t width;
  uint16_t height;
};

// Low-level functions
int ssd1683_init(const struct ssd1683_config *cfg);
void ssd1683_reset(const struct ssd1683_config *cfg);
void ssd1683_write_cmd(const struct ssd1683_config *cfg, uint8_t cmd);
void ssd1683_write_data(const struct ssd1683_config *cfg, uint8_t data);
void ssd1683_clear(const struct ssd1683_config *cfg);
void ssd1683_refresh(const struct ssd1683_config *cfg);
void ssd1683_deep_sleep(const struct ssd1683_config *cfg);

void ssd1683_set_pixel_fb(int x, int y, uint8_t color);
void ssd1683_draw_rect_fb(int x, int y, int w, int h, uint8_t color);
void ssd1683_flush(const struct ssd1683_config *cfg);

void ssd1683_draw_bitmap(const struct ssd1683_config *cfg);
#endif // SSD1683_H
