/*****************************************************************************
 * |File	:	epd_config.h
 * |Author	:	Adapted for nRF52840DK
 * |Function	:	Hardware configuration for 4.2" EPD V2
 * |Info	:	Pin configuration for nRF52840DK
 ****************************************************************************/
#ifndef _EPD_CONFIG_H_
#define _EPD_CONFIG_H_

#include <stdint.h>

/* Pin Configuration for nRF52840DK */
#define EPD_BUSY_PIN 30 // GPIO0_30
#define EPD_DC_PIN 11   // GPIO1_11
#define EPD_RST_PIN 3   // GPIO1_3
#define EPD_CS_PIN 12   // GPIO1_12

/* SPI Configuration */
#define EPD_SPI_FREQ 4000000 // 4MHz SPI frequency
#define EPD_SPI_MODE 0       // SPI Mode 0

/* Display Parameters */
#define EPD_WIDTH 400
#define EPD_HEIGHT 300
#define EPD_BUFFER_SIZE (EPD_WIDTH * EPD_HEIGHT / 8)

/* Pin level definitions */
#define LOW 0
#define HIGH 1

/* Hardware abstraction functions */
void epd_gpio_init(void);
void epd_gpio_write(uint8_t pin, uint8_t value);
uint8_t epd_gpio_read(uint8_t pin);
void epd_spi_init(void);
void epd_spi_write_byte(uint8_t data);
void epd_spi_write_buffer(uint8_t *data, uint32_t length);
void epd_delay_ms(uint32_t ms);

/* Hardware status functions */
bool epd_hardware_is_initialized(void);
void epd_hardware_deinit(void);

/* Internal pin definitions */
#define EPD_CS_PIN_IDX 0
#define EPD_RST_PIN_IDX 1
#define EPD_DC_PIN_IDX 2
#define EPD_BUSY_PIN_IDX 3

#endif
