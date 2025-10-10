#ifndef SSD1683_H
#define SSD1683_H

// #include "image_bitmap.h" // Contains bw_bitmap[15000] - file deleted
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
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

// Runtime data structure
struct ssd1683_data {
  enum display_pixel_format pixel_format;
  enum display_orientation orientation;
};

// Display driver API structure
extern const struct display_driver_api ssd1683_display_api;

// Low-level functions
int ssd1683_init(const struct ssd1683_config *cfg);
int ssd1683_init_fast(const struct ssd1683_config *cfg);
int ssd1683_init_gray(const struct ssd1683_config *cfg);
void ssd1683_reset(const struct ssd1683_config *cfg);
void ssd1683_write_cmd(const struct ssd1683_config *cfg, uint8_t cmd);
void ssd1683_write_data(const struct ssd1683_config *cfg, uint8_t data);
void ssd1683_clear(const struct ssd1683_config *cfg);
void ssd1683_refresh(const struct ssd1683_config *cfg);
void ssd1683_refresh_fast(const struct ssd1683_config *cfg);
void ssd1683_refresh_partial(const struct ssd1683_config *cfg);
void ssd1683_refresh_gray(const struct ssd1683_config *cfg);
void ssd1683_deep_sleep(const struct ssd1683_config *cfg);

void ssd1683_flush(const struct ssd1683_config *cfg, uint8_t *image_buffer);
void ssd1683_flush_from_paint(const struct ssd1683_config *cfg,
                              uint8_t *wb_buffer, uint8_t *rw_buffer);

// Display functions
void ssd1683_display_fast(const struct ssd1683_config *cfg, uint8_t *image);
void ssd1683_display_4gray(const struct ssd1683_config *cfg, uint8_t *image);
void ssd1683_partial_display(const struct ssd1683_config *cfg, uint16_t x,
                             uint16_t y, uint16_t w, uint16_t l,
                             uint8_t *image);
void ssd1683_write_display(const struct ssd1683_config *cfg, uint16_t x,
                           uint16_t y, uint16_t w, uint16_t l, uint8_t *image);

// TurnOnDisplay functions are identical to refresh functions - use those
// instead

// Zephyr Display Driver API functions
int ssd1683_blanking_on(const struct device *dev);
int ssd1683_blanking_off(const struct device *dev);
int ssd1683_write(const struct device *dev, const uint16_t x, const uint16_t y,
                  const struct display_buffer_descriptor *desc,
                  const void *buf);
int ssd1683_read(const struct device *dev, const uint16_t x, const uint16_t y,
                 const struct display_buffer_descriptor *desc, void *buf);
int ssd1683_get_capabilities(const struct device *dev,
                             struct display_capabilities *capabilities);
int ssd1683_set_pixel_format(const struct device *dev,
                             const enum display_pixel_format pixel_format);
int ssd1683_set_orientation(const struct device *dev,
                            const enum display_orientation orientation);

#endif // SSD1683_H
