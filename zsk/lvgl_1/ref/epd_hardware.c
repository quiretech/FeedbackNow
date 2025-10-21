/*****************************************************************************
 * |File	:	epd_hardware.c
 * |Author	:	Hardware abstraction layer for 4.2" EPD V2
 * |Function	:	GPIO and SPI implementation for nRF52840DK
 * |Info	:	Standalone hardware abstraction
 ****************************************************************************/
#include "epd_config.h"
#include <stdint.h>
#include <string.h>

#ifdef CONFIG_ZEPHYR
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(epd_hardware);
#else
#include <stdio.h>
#include <unistd.h>
#define LOG_DBG(fmt, ...) printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) printf("[ERR] " fmt "\n", ##__VA_ARGS__)
#endif

/* Hardware state */
static bool hardware_initialized = false;

#ifdef CONFIG_ZEPHYR
/* Zephyr device references */
static const struct device *gpio_dev;
static const struct device *spi_dev;

/* GPIO pin configurations */
static struct gpio_dt_spec gpio_pins[] = {
    [EPD_CS_PIN_IDX] = GPIO_DT_SPEC_INST_GET_BY_IDX(0, cs_gpios, 0),
    [EPD_RST_PIN_IDX] = GPIO_DT_SPEC_INST_GET_BY_IDX(0, reset_gpios, 0),
    [EPD_DC_PIN_IDX] = GPIO_DT_SPEC_INST_GET_BY_IDX(0, dc_gpios, 0),
    [EPD_BUSY_PIN_IDX] = GPIO_DT_SPEC_INST_GET_BY_IDX(0, busy_gpios, 0),
};

/* SPI configuration */
static struct spi_config spi_cfg = {
    .frequency = EPD_SPI_FREQ,
    .operation =
        SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
    .slave = 0,
    .cs = NULL,
};
#endif

/******************************************************************************
 * GPIO Functions
 *****************************************************************************/
void epd_gpio_init(void) {
#ifdef CONFIG_ZEPHYR
  /* Get GPIO device */
  gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
  if (!device_is_ready(gpio_dev)) {
    LOG_ERR("GPIO device not ready");
    return;
  }

  /* Configure GPIO pins */
  for (int i = 0; i < 4; i++) {
    if (gpio_pins[i].port) {
      gpio_pin_configure_dt(&gpio_pins[i], GPIO_OUTPUT_INACTIVE);
    }
  }

  /* Configure BUSY pin as input */
  if (gpio_pins[EPD_BUSY_PIN_IDX].port) {
    gpio_pin_configure_dt(&gpio_pins[EPD_BUSY_PIN_IDX], GPIO_INPUT);
  }

  LOG_DBG("GPIO initialized");
#else
  /* For standalone implementation, you would configure your GPIO here */
  LOG_DBG("GPIO initialized (standalone mode)");
#endif
  hardware_initialized = true;
}

void epd_gpio_write(uint8_t pin, uint8_t value) {
  if (!hardware_initialized) {
    LOG_ERR("Hardware not initialized");
    return;
  }

#ifdef CONFIG_ZEPHYR
  /* Map pin number to GPIO index */
  struct gpio_dt_spec *gpio_spec = NULL;

  switch (pin) {
  case EPD_CS_PIN:
    gpio_spec = &gpio_pins[EPD_CS_PIN_IDX];
    break;
  case EPD_RST_PIN:
    gpio_spec = &gpio_pins[EPD_RST_PIN_IDX];
    break;
  case EPD_DC_PIN:
    gpio_spec = &gpio_pins[EPD_DC_PIN_IDX];
    break;
  default:
    LOG_ERR("Invalid GPIO pin: %d", pin);
    return;
  }

  if (gpio_spec && gpio_spec->port) {
    gpio_pin_set_dt(gpio_spec, value);
  }
#else
  /* For standalone implementation, write to your GPIO registers */
  LOG_DBG("GPIO write: pin=%d, value=%d", pin, value);
#endif
}

uint8_t epd_gpio_read(uint8_t pin) {
  if (!hardware_initialized) {
    LOG_ERR("Hardware not initialized");
    return 0;
  }

#ifdef CONFIG_ZEPHYR
  if (pin == EPD_BUSY_PIN) {
    if (gpio_pins[EPD_BUSY_PIN_IDX].port) {
      return gpio_pin_get_dt(&gpio_pins[EPD_BUSY_PIN_IDX]);
    }
  }
  LOG_ERR("Invalid GPIO read pin: %d", pin);
  return 0;
#else
  /* For standalone implementation, read from your GPIO registers */
  LOG_DBG("GPIO read: pin=%d", pin);
  return 0; /* Return actual GPIO state */
#endif
}

/******************************************************************************
 * SPI Functions
 *****************************************************************************/
void epd_spi_init(void) {
#ifdef CONFIG_ZEPHYR
  /* Get SPI device */
  spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));
  if (!device_is_ready(spi_dev)) {
    LOG_ERR("SPI device not ready");
    return;
  }

  LOG_DBG("SPI initialized");
#else
  /* For standalone implementation, configure your SPI peripheral */
  LOG_DBG("SPI initialized (standalone mode)");
#endif
}

void epd_spi_write_byte(uint8_t data) {
  if (!hardware_initialized) {
    LOG_ERR("Hardware not initialized");
    return;
  }

#ifdef CONFIG_ZEPHYR
  struct spi_buf tx_buf = {
      .buf = &data,
      .len = 1,
  };
  struct spi_buf_set tx_set = {
      .buffers = &tx_buf,
      .count = 1,
  };

  int ret = spi_write(spi_dev, &spi_cfg, &tx_set);
  if (ret) {
    LOG_ERR("SPI write failed: %d", ret);
  }
#else
  /* For standalone implementation, write to your SPI peripheral */
  LOG_DBG("SPI write byte: 0x%02X", data);
#endif
}

void epd_spi_write_buffer(uint8_t *data, uint32_t length) {
  if (!hardware_initialized || !data) {
    LOG_ERR("Invalid parameters");
    return;
  }

#ifdef CONFIG_ZEPHYR
  struct spi_buf tx_buf = {
      .buf = data,
      .len = length,
  };
  struct spi_buf_set tx_set = {
      .buffers = &tx_buf,
      .count = 1,
  };

  int ret = spi_write(spi_dev, &spi_cfg, &tx_set);
  if (ret) {
    LOG_ERR("SPI write buffer failed: %d", ret);
  }
#else
  /* For standalone implementation, write buffer to your SPI peripheral */
  LOG_DBG("SPI write buffer: %d bytes", length);
  for (uint32_t i = 0; i < length; i++) {
    LOG_DBG("  [%d]: 0x%02X", i, data[i]);
  }
#endif
}

/******************************************************************************
 * Delay Functions
 *****************************************************************************/
void epd_delay_ms(uint32_t ms) {
#ifdef CONFIG_ZEPHYR
  k_msleep(ms);
#else
  /* For standalone implementation, use your delay function */
  usleep(ms * 1000); /* Convert ms to microseconds */
#endif
}

/******************************************************************************
 * Hardware Status
 *****************************************************************************/
bool epd_hardware_is_initialized(void) { return hardware_initialized; }

void epd_hardware_deinit(void) {
  hardware_initialized = false;
  LOG_DBG("Hardware deinitialized");
}
