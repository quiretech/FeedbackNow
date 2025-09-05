#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(epd1683, LOG_LEVEL_DBG);

/* SSD1683 Commands */
#define SSD1683_CMD_DRIVER_OUTPUT_CONTROL 0x01
#define SSD1683_CMD_GATE_DRIVING_VOLTAGE 0x03
#define SSD1683_CMD_SOURCE_DRIVING_VOLTAGE 0x04
#define SSD1683_CMD_PROGRAMMING_MODE 0x08
#define SSD1683_CMD_BOOSTER_SOFT_START 0x0C
#define SSD1683_CMD_DEEP_SLEEP_MODE 0x10
#define SSD1683_CMD_DATA_ENTRY_MODE 0x11
#define SSD1683_CMD_SW_RESET 0x12
#define SSD1683_CMD_TEMPERATURE_SENSOR 0x1A
#define SSD1683_CMD_MASTER_ACTIVATION 0x20
#define SSD1683_CMD_DISPLAY_UPDATE_CONTROL_1 0x21
#define SSD1683_CMD_DISPLAY_UPDATE_CONTROL_2 0x22
#define SSD1683_CMD_WRITE_RAM_BW 0x24
#define SSD1683_CMD_WRITE_RAM_RED 0x26
#define SSD1683_CMD_VCOM_SENSE 0x28
#define SSD1683_CMD_VCOM_SENSE_DURATION 0x29
#define SSD1683_CMD_PROGRAM_VCOM_OTP 0x2A
#define SSD1683_CMD_VCOM_WRITE_CONTROL 0x2C
#define SSD1683_CMD_VCOM_WRITE 0x2D
#define SSD1683_CMD_OTP_PROGRAM_READ 0x2E
#define SSD1683_CMD_USER_ID_READ 0x2F
#define SSD1683_CMD_STATUS_BIT_READ 0x2F
#define SSD1683_CMD_PROGRAM_WS_OTP 0x30
#define SSD1683_CMD_LOAD_WS_OTP 0x31
#define SSD1683_CMD_WRITE_LUT_REGISTER 0x32
#define SSD1683_CMD_CRC_CALCULATION 0x34
#define SSD1683_CMD_CRC_STATUS_READ 0x35
#define SSD1683_CMD_PROGRAM_OTP_SELECTION 0x36
#define SSD1683_CMD_WRITE_DUMMY 0x3A
#define SSD1683_CMD_WRITE_GATE_LINE_WIDTH 0x3B
#define SSD1683_CMD_WRITE_BORDER_WAVEFORM 0x3C
#define SSD1683_CMD_SET_RAM_X_ADDRESS_START 0x44
#define SSD1683_CMD_SET_RAM_X_ADDRESS_END 0x45
#define SSD1683_CMD_SET_RAM_Y_ADDRESS_START 0x4E
#define SSD1683_CMD_SET_RAM_Y_ADDRESS_END 0x4F
#define SSD1683_CMD_SET_RAM_X_ADDRESS_COUNTER 0x4E
#define SSD1683_CMD_SET_RAM_Y_ADDRESS_COUNTER 0x4F
#define SSD1683_CMD_TERMINATE_FRAME_READ_WRITE 0xFF

/* Device configuration struct */
struct epd1683_config {
  struct spi_dt_spec spi;
  struct gpio_dt_spec reset_gpio;
  struct gpio_dt_spec dc_gpio;
  struct gpio_dt_spec busy_gpio;
  uint16_t width;
  uint16_t height;
};

/* Device data struct */
struct epd1683_data {
  uint8_t *framebuffer;
  bool initialized;
};

/* GPIO helpers */
static void epd_reset(const struct device *dev) {
  const struct epd1683_config *config = dev->config;

  gpio_pin_set_dt(&config->reset_gpio, 1);
  k_msleep(10);
  gpio_pin_set_dt(&config->reset_gpio, 0);
  k_msleep(10);
  gpio_pin_set_dt(&config->reset_gpio, 1);
  k_msleep(10);
}

static void epd_wait_busy(const struct device *dev) {
  const struct epd1683_config *config = dev->config;

  while (gpio_pin_get_dt(&config->busy_gpio)) {
    k_msleep(5);
  }
}

/* SPI helpers */
static int epd_write_cmd(const struct device *dev, uint8_t cmd) {
  const struct epd1683_config *config = dev->config;

  gpio_pin_set_dt(&config->dc_gpio, 0); /* command */
  return spi_write_dt(&config->spi, &cmd, 1);
}

static int epd_write_data(const struct device *dev, const uint8_t *data,
                          size_t len) {
  const struct epd1683_config *config = dev->config;

  gpio_pin_set_dt(&config->dc_gpio, 1); /* data */
  return spi_write_dt(&config->spi, data, len);
}

/* SSD1683 Initialization sequence */
static int epd1683_init_sequence(const struct device *dev) {
  const struct epd1683_config *config = dev->config;
  int ret;

  LOG_DBG("Starting SSD1683 initialization");

  /* Software reset */
  ret = epd_write_cmd(dev, SSD1683_CMD_SW_RESET);
  if (ret)
    return ret;
  epd_wait_busy(dev);

  /* Driver output control */
  uint8_t driver_output[] = {0xC7, 0x00, 0x00};
  ret = epd_write_cmd(dev, SSD1683_CMD_DRIVER_OUTPUT_CONTROL);
  if (ret)
    return ret;
  ret = epd_write_data(dev, driver_output, sizeof(driver_output));
  if (ret)
    return ret;

  /* Data entry mode */
  ret = epd_write_cmd(dev, SSD1683_CMD_DATA_ENTRY_MODE);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0x01}, 1);
  if (ret)
    return ret;

  /* Set RAM X address start/end */
  ret = epd_write_cmd(dev, SSD1683_CMD_SET_RAM_X_ADDRESS_START);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0x00}, 1);
  if (ret)
    return ret;

  ret = epd_write_cmd(dev, SSD1683_CMD_SET_RAM_X_ADDRESS_END);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0x18}, 1);
  if (ret)
    return ret;

  /* Set RAM Y address start/end */
  ret = epd_write_cmd(dev, SSD1683_CMD_SET_RAM_Y_ADDRESS_START);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0xC7, 0x00}, 2);
  if (ret)
    return ret;

  ret = epd_write_cmd(dev, SSD1683_CMD_SET_RAM_Y_ADDRESS_END);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0x00, 0x00}, 2);
  if (ret)
    return ret;

  /* Booster soft start */
  ret = epd_write_cmd(dev, SSD1683_CMD_BOOSTER_SOFT_START);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0xD7, 0xD6, 0x9D}, 3);
  if (ret)
    return ret;

  /* VCOM voltage */
  ret = epd_write_cmd(dev, SSD1683_CMD_VCOM_WRITE);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0xA8}, 1);
  if (ret)
    return ret;

  /* Gate driving voltage */
  ret = epd_write_cmd(dev, SSD1683_CMD_GATE_DRIVING_VOLTAGE);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0xE8}, 1);
  if (ret)
    return ret;

  /* Source driving voltage */
  ret = epd_write_cmd(dev, SSD1683_CMD_SOURCE_DRIVING_VOLTAGE);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0x41, 0xA8, 0x32}, 3);
  if (ret)
    return ret;

  /* Display update control 1 */
  ret = epd_write_cmd(dev, SSD1683_CMD_DISPLAY_UPDATE_CONTROL_1);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0x00, 0x80}, 2);
  if (ret)
    return ret;

  /* Display update control 2 */
  ret = epd_write_cmd(dev, SSD1683_CMD_DISPLAY_UPDATE_CONTROL_2);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){0xCF}, 1);
  if (ret)
    return ret;

  LOG_DBG("SSD1683 initialization complete");
  return 0;
}

/* Display API callbacks */
static int epd1683_blanking_on(const struct device *dev) {
  return 0; /* Not used for EPD */
}

static int epd1683_blanking_off(const struct device *dev) {
  return 0; /* Not used for EPD */
}

static int epd1683_write(const struct device *dev, const uint16_t x,
                         const uint16_t y, const uint16_t w, const uint16_t h,
                         const void *buf) {
  const struct epd1683_config *config = dev->config;
  struct epd1683_data *data = dev->data;
  const uint8_t *pixels = buf;
  int ret;

  if (!data->initialized) {
    LOG_ERR("Display not initialized");
    return -EINVAL;
  }

  /* Set RAM X address counter */
  ret = epd_write_cmd(dev, SSD1683_CMD_SET_RAM_X_ADDRESS_COUNTER);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){x / 8}, 1);
  if (ret)
    return ret;

  /* Set RAM Y address counter */
  ret = epd_write_cmd(dev, SSD1683_CMD_SET_RAM_Y_ADDRESS_COUNTER);
  if (ret)
    return ret;
  ret = epd_write_data(dev, (uint8_t[]){y & 0xFF, (y >> 8) & 0xFF}, 2);
  if (ret)
    return ret;

  /* Convert and write pixel data */
  size_t bytes = (w * h + 7) / 8;
  uint8_t line[bytes];

  /* Convert framebuffer to SSD1683 format */
  for (size_t i = 0; i < bytes; i++) {
    line[i] = 0x00;
    for (int bit = 0; bit < 8; bit++) {
      int idx = i * 8 + bit;
      if (idx >= w * h)
        break;
      uint8_t pix = pixels[idx];
      if (pix == 1) { /* black */
        line[i] |= (1 << (7 - bit));
      }
    }
  }

  /* Write to RAM */
  ret = epd_write_cmd(dev, SSD1683_CMD_WRITE_RAM_BW);
  if (ret)
    return ret;
  ret = epd_write_data(dev, line, bytes);
  if (ret)
    return ret;

  /* Trigger display update */
  ret = epd_write_cmd(dev, SSD1683_CMD_MASTER_ACTIVATION);
  if (ret)
    return ret;
  epd_wait_busy(dev);

  return 0;
}

static int epd1683_read(const struct device *dev, const uint16_t x,
                        const uint16_t y, const uint16_t w, const uint16_t h,
                        void *buf) {
  return -ENOTSUP; /* Not supported for EPD */
}

static void *epd1683_get_framebuffer(const struct device *dev) {
  struct epd1683_data *data = dev->data;
  return data->framebuffer;
}

static int epd1683_set_brightness(const struct device *dev,
                                  const uint8_t brightness) {
  return -ENOTSUP; /* Not supported for EPD */
}

static int epd1683_set_contrast(const struct device *dev,
                                const uint8_t contrast) {
  return -ENOTSUP; /* Not supported for EPD */
}

static void
epd1683_get_capabilities(const struct device *dev,
                         struct display_capabilities *capabilities) {
  const struct epd1683_config *config = dev->config;

  capabilities->x_resolution = config->width;
  capabilities->y_resolution = config->height;
  capabilities->supported_pixel_formats = PIXEL_FORMAT_MONO10;
  capabilities->current_pixel_format = PIXEL_FORMAT_MONO10;
  capabilities->screen_info = SCREEN_INFO_MONO_VTILED;
  capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

/* Display driver API struct */
static const struct display_driver_api epd1683_api = {
    .blanking_on = epd1683_blanking_on,
    .blanking_off = epd1683_blanking_off,
    .write = epd1683_write,
    .read = epd1683_read,
    .get_framebuffer = epd1683_get_framebuffer,
    .set_brightness = epd1683_set_brightness,
    .set_contrast = epd1683_set_contrast,
    .get_capabilities = epd1683_get_capabilities,
};

/* Device init function */
static int epd1683_init(const struct device *dev) {
  const struct epd1683_config *config = dev->config;
  struct epd1683_data *data = dev->data;
  int ret;

  LOG_DBG("Initializing EPD1683 display");

  /* Check if SPI device is ready */
  if (!spi_is_ready_dt(&config->spi)) {
    LOG_ERR("SPI device not ready");
    return -ENODEV;
  }

  /* Configure GPIO pins */
  ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_ACTIVE);
  if (ret) {
    LOG_ERR("Failed to configure reset GPIO: %d", ret);
    return ret;
  }

  ret = gpio_pin_configure_dt(&config->dc_gpio, GPIO_OUTPUT_INACTIVE);
  if (ret) {
    LOG_ERR("Failed to configure DC GPIO: %d", ret);
    return ret;
  }

  ret = gpio_pin_configure_dt(&config->busy_gpio, GPIO_INPUT);
  if (ret) {
    LOG_ERR("Failed to configure busy GPIO: %d", ret);
    return ret;
  }

  /* Allocate framebuffer */
  size_t fb_size = (config->width * config->height + 7) / 8;
  data->framebuffer = k_malloc(fb_size);
  if (!data->framebuffer) {
    LOG_ERR("Failed to allocate framebuffer");
    return -ENOMEM;
  }
  memset(data->framebuffer, 0xFF, fb_size); /* Initialize to white */

  /* Hardware reset */
  epd_reset(dev);
  epd_wait_busy(dev);

  /* Initialize display */
  ret = epd1683_init_sequence(dev);
  if (ret) {
    LOG_ERR("Failed to initialize display: %d", ret);
    return ret;
  }

  data->initialized = true;
  LOG_INF("EPD1683 display initialized successfully");

  return 0;
}

/* Device configuration */
#define EPD1683_DEFINE(inst)                                                   \
  static struct epd1683_data epd1683_data_##inst;                              \
  static const struct epd1683_config epd1683_config_##inst = {                 \
      .spi =                                                                   \
          SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),   \
      .reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                  \
      .dc_gpio = GPIO_DT_SPEC_INST_GET(inst, dc_gpios),                        \
      .busy_gpio = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),                    \
      .width = DT_INST_PROP(inst, width),                                      \
      .height = DT_INST_PROP(inst, height),                                    \
  };                                                                           \
  DEVICE_DT_INST_DEFINE(inst, epd1683_init, NULL, &epd1683_data_##inst,        \
                        &epd1683_config_##inst, POST_KERNEL,                   \
                        CONFIG_DISPLAY_INIT_PRIORITY, &epd1683_api);

DT_INST_FOREACH_STATUS_OKAY(EPD1683_DEFINE)
