#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(epd1683, LOG_LEVEL_DBG);

/* Device data struct */
struct epd1683_data {
  const struct device *spi_dev;
  struct spi_config spi_cfg;

  const struct device *reset_gpio;
  const struct device *dc_gpio;
  const struct device *busy_gpio;

  gpio_pin_t reset_pin;
  gpio_pin_t dc_pin;
  gpio_pin_t busy_pin;

  uint16_t width;
  uint16_t height;
};

/* GPIO helpers */
static void epd_reset(struct epd1683_data *epd) {
  gpio_pin_set(epd->reset_gpio, epd->reset_pin, 1);
  k_msleep(10);
  gpio_pin_set(epd->reset_gpio, epd->reset_pin, 0);
  k_msleep(10);
  gpio_pin_set(epd->reset_gpio, epd->reset_pin, 1);
  k_msleep(10);
}

static void epd_wait_busy(struct epd1683_data *epd) {
  while (gpio_pin_get(epd->busy_gpio, epd->busy_pin)) {
    k_msleep(5);
  }
}

/* SPI helpers */
static int epd_write_cmd(struct epd1683_data *epd, uint8_t cmd) {
  gpio_pin_set(epd->dc_gpio, epd->dc_pin, 0); /* command */
  return spi_write(epd->spi_dev, &epd->spi_cfg, &cmd, 1);
}

static int epd_write_data(struct epd1683_data *epd, const uint8_t *data,
                          size_t len) {
  gpio_pin_set(epd->dc_gpio, epd->dc_pin, 1); /* data */
  return spi_write(epd->spi_dev, &epd->spi_cfg, data, len);
}

/* Display API callbacks */
static int epd1683_blanking_on(const struct device *dev) {
  return 0; /* Not used for EPD */
}

static int epd1683_blanking_off(const struct device *dev) { return 0; }

/* Convert framebuffer (1 byte per pixel: 0=white, 1=black, 2=red) to SSD1683
 * format */
static int epd1683_write(const struct device *dev, const uint16_t x,
                         const uint16_t y, const uint16_t w, const uint16_t h,
                         const void *buf) {
  struct epd1683_data *epd = dev->data;
  const uint8_t *pixels = buf;
  size_t i, bytes = (w * h + 7) / 8;
  uint8_t line[bytes];

  /* Simple example: pack black/white only; red ignored here */
  for (i = 0; i < bytes; i++) {
    line[i] = 0x00;
    for (int bit = 0; bit < 8; bit++) {
      int idx = i * 8 + bit;
      if (idx >= w * h)
        break;
      uint8_t pix = pixels[idx];
      if (pix == 1) { /* black */
        line[i] |= (1 << (7 - bit));
      }
      /* red = 2 ignored for now */
    }
  }

  /* Send example commands: full update */
  epd_write_cmd(epd, 0x24); /* write RAM command */
  epd_write_data(epd, line, bytes);

  epd_write_cmd(epd, 0x20); /* display update command */
  epd_wait_busy(epd);

  return 0;
}

/* Display driver API struct */
static const struct display_driver_api epd1683_api = {
    .blanking_on = epd1683_blanking_on,
    .blanking_off = epd1683_blanking_off,
    .write = epd1683_write,
};

/* Device init function */
static int epd1683_init(const struct device *dev) {
  struct epd1683_data *epd = dev->data;

  if (!device_is_ready(epd->spi_dev) || !device_is_ready(epd->reset_gpio) ||
      !device_is_ready(epd->dc_gpio) || !device_is_ready(epd->busy_gpio)) {
    LOG_ERR("One of the required devices not ready");
    return -ENODEV;
  }

  epd_reset(epd);
  epd_wait_busy(epd);

  /* TODO: send full SSD1683 init sequence (booster, VCOM, LUTs, etc.) */

  return 0;
}

/* Device data instance */
static struct epd1683_data epd1683_data = {
    .spi_dev = DEVICE_DT_GET(DT_NODELABEL(epd_ssd1683)),
    .spi_cfg =
        {
            .frequency = 4000000,
            .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
            .slave = 0,
            .cs = NULL,
        },
    .reset_gpio =
        DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(epd_ssd1683), reset_gpios)),
    .dc_gpio = DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(epd_ssd1683), dc_gpios)),
    .busy_gpio =
        DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(epd_ssd1683), busy_gpios)),
    .reset_pin = DT_GPIO_PIN(DT_NODELABEL(epd_ssd1683), reset_gpios),
    .dc_pin = DT_GPIO_PIN(DT_NODELABEL(epd_ssd1683), dc_gpios),
    .busy_pin = DT_GPIO_PIN(DT_NODELABEL(epd_ssd1683), busy_gpios),
    .width = 400,
    .height = 300,
};

/* Device declaration macro */
DEVICE_DT_DEFINE(DT_NODELABEL(epd_ssd1683), epd1683_init, NULL, &epd1683_data,
                 NULL, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY,
                 &epd1683_api);
