/*****************************************************************************
 * |File	:	epd_4in2_v2.c
 * |Author	:	Waveshare team (ported for standalone use)
 * |Function	:	4.2inch e-paper V2 Driver
 * |Info	:	Standalone driver for nRF52840DK with Zephyr
 *compatibility
 *----------------
 * |This version:	V2.0
 * |Date	:	2025-01-08
 * |Info	:	Extracted and adapted from Nordic ESL project
 ****************************************************************************/
#include "epd_4in2_v2.h"
#include "epd_config.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(epd_4in2_v2);
#else
#include <stdio.h>
#define LOG_DBG(fmt, ...) printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#endif

/* LUT for 4-level grayscale */
const unsigned char LUT_ALL[233] = {
    0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01, 0x01, 0x05, 0x0A, 0x01, 0x0A, 0x01,
    0x01, 0x01, 0x05, 0x08, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x02, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01,
    0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01, 0x05, 0x48, 0x03, 0x82,
    0x84, 0x01, 0x01, 0x01, 0x84, 0x84, 0x82, 0x00, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x0A, 0x1B, 0x8F, 0x03, 0x01, 0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01,
    0x01, 0x01, 0x05, 0x48, 0x83, 0x82, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x02, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x8A, 0x1B, 0x8F, 0x03, 0x01,
    0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01, 0x05, 0x48, 0x83, 0x02,
    0x04, 0x01, 0x01, 0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x8A, 0x9B, 0x8F, 0x03, 0x01, 0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01,
    0x01, 0x01, 0x05, 0x48, 0x03, 0x42, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x42, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x07,
    0x17, 0x41, 0xA8, 0x32, 0x30,
};

/* Internal state */
static epd_refresh_mode_t current_refresh_mode = EPD_REFRESH_FULL;

/******************************************************************************
 * function :	Software reset
 * parameter:
 *****************************************************************************/
static void epd_4in2_v2_reset(void) {
  epd_gpio_write(EPD_RST_PIN, HIGH);
  epd_delay_ms(100);
  epd_gpio_write(EPD_RST_PIN, LOW);
  epd_delay_ms(2);
  epd_gpio_write(EPD_RST_PIN, HIGH);
  epd_delay_ms(100);
}

/******************************************************************************
 * function :	send command
 * parameter:
 *	Reg : Command register
 *****************************************************************************/
static void epd_4in2_v2_send_command(uint8_t reg) {
  epd_gpio_write(EPD_DC_PIN, LOW);
  epd_gpio_write(EPD_CS_PIN, LOW);
  epd_spi_write_byte(reg);
  epd_gpio_write(EPD_CS_PIN, HIGH);
}

/******************************************************************************
 * function :	send data
 * parameter:
 *     Data : Write data
 *****************************************************************************/
static void epd_4in2_v2_send_data(uint8_t data) {
  epd_gpio_write(EPD_DC_PIN, HIGH);
  epd_gpio_write(EPD_CS_PIN, LOW);
  epd_spi_write_byte(data);
  epd_gpio_write(EPD_CS_PIN, HIGH);
}

/******************************************************************************
 * function :	Wait until the busy_pin goes LOW
 * parameter:
 *****************************************************************************/
void epd_4in2_v2_read_busy(void) {
  LOG_DBG("e-Paper busy\r\n");
  while (epd_gpio_read(EPD_BUSY_PIN) == HIGH) { /* LOW: idle, HIGH: busy */
    epd_delay_ms(10);
  }
  LOG_DBG("e-Paper busy release\r\n");
}

/******************************************************************************
 * function :	Turn On Display
 * parameter:
 *****************************************************************************/
static void epd_4in2_v2_turn_on_display(void) {
  epd_4in2_v2_send_command(0x22);
  epd_4in2_v2_send_data(0xF7);
  epd_4in2_v2_send_command(0x20);
  epd_4in2_v2_read_busy();
}

void epd_4in2_v2_turn_on_display_fast(void) {
  epd_4in2_v2_send_command(0x22);
  epd_4in2_v2_send_data(0xC7);
  epd_4in2_v2_send_command(0x20);
  epd_4in2_v2_read_busy();
}

static void epd_4in2_v2_turn_on_display_partial(void) {
  epd_4in2_v2_send_command(0x22);
  epd_4in2_v2_send_data(0xFF);
  epd_4in2_v2_send_command(0x20);
  epd_4in2_v2_read_busy();
}

static void epd_4in2_v2_turn_on_display_4gray(void) {
  epd_4in2_v2_send_command(0x22);
  epd_4in2_v2_send_data(0xCF);
  epd_4in2_v2_send_command(0x20);
  epd_4in2_v2_read_busy();
}

/******************************************************************************
 * function :	Setting the display window
 * parameter:
 *****************************************************************************/
static void epd_4in2_v2_set_windows(uint16_t xstart, uint16_t ystart,
                                    uint16_t xend, uint16_t yend) {
  epd_4in2_v2_send_command(0x44); /* SET_RAM_X_ADDRESS_START_END_POSITION */
  epd_4in2_v2_send_data((xstart >> 3) & 0xFF);
  epd_4in2_v2_send_data((xend >> 3) & 0xFF);

  epd_4in2_v2_send_command(0x45); /* SET_RAM_Y_ADDRESS_START_END_POSITION */
  epd_4in2_v2_send_data(ystart & 0xFF);
  epd_4in2_v2_send_data((ystart >> 8) & 0xFF);
  epd_4in2_v2_send_data(yend & 0xFF);
  epd_4in2_v2_send_data((yend >> 8) & 0xFF);
}

/******************************************************************************
 * function :	Set Cursor
 * parameter:
 *****************************************************************************/
static void epd_4in2_v2_set_cursor(uint16_t xstart, uint16_t ystart) {
  epd_4in2_v2_send_command(0x4E); /* SET_RAM_X_ADDRESS_COUNTER */
  epd_4in2_v2_send_data(xstart & 0xFF);

  epd_4in2_v2_send_command(0x4F); /* SET_RAM_Y_ADDRESS_COUNTER */
  epd_4in2_v2_send_data(ystart & 0xFF);
  epd_4in2_v2_send_data((ystart >> 8) & 0xFF);
}

/* LUT download for 4-level grayscale */
static void epd_4in2_v2_4gray_lut(void) {
  unsigned char i;

  /* WS byte 0~152, the content of VS[nX-LUTm], TP[nX], RP[n], SR[nXY], FR[n]
   * and XON[nXY] */
  epd_4in2_v2_send_command(0x32);
  for (i = 0; i < 227; i++) {
    epd_4in2_v2_send_data(LUT_ALL[i]);
  }

  /* WS byte 153, the content of Option for LUT end */
  epd_4in2_v2_send_command(0x3F);
  epd_4in2_v2_send_data(LUT_ALL[i++]);

  /* WS byte 154, the content of gate leveL */
  epd_4in2_v2_send_command(0x03);
  epd_4in2_v2_send_data(LUT_ALL[i++]); /* VGH */

  /* WS byte 155~157, the content of source level */
  epd_4in2_v2_send_command(0x04);
  epd_4in2_v2_send_data(LUT_ALL[i++]); /* VSH1 */
  epd_4in2_v2_send_data(LUT_ALL[i++]); /* VSH2 */
  epd_4in2_v2_send_data(LUT_ALL[i++]); /* VSL */

  /* WS byte 158, the content of VCOM level */
  epd_4in2_v2_send_command(0x2c);
  epd_4in2_v2_send_data(LUT_ALL[i++]); /* VCOM */
}

/******************************************************************************
 * function :	Initialize the e-Paper register
 * parameter:
 *****************************************************************************/
void epd_4in2_v2_init(void) {
  epd_4in2_v2_reset();

  epd_4in2_v2_read_busy();
  epd_4in2_v2_send_command(0x12); /* soft  reset */
  epd_4in2_v2_read_busy();

  epd_4in2_v2_send_command(0x21); /*  Display update control */
  epd_4in2_v2_send_data(0x40);
  epd_4in2_v2_send_data(0x00);

  epd_4in2_v2_send_command(0x3C); /* BorderWavefrom */
  epd_4in2_v2_send_data(0x05);

  epd_4in2_v2_send_command(0x11); /* data  entry  mode */
  epd_4in2_v2_send_data(0x03);    /* X-mode */

  epd_4in2_v2_set_windows(0, 0, EPD_4IN2_V2_WIDTH - 1, EPD_4IN2_V2_HEIGHT - 1);

  epd_4in2_v2_set_cursor(0, 0);

  epd_4in2_v2_read_busy();
}

/******************************************************************************
 * function :	Initialize Fast the e-Paper register
 * parameter:
 *****************************************************************************/
void epd_4in2_v2_init_fast(void) {
  epd_4in2_v2_reset();

  epd_4in2_v2_read_busy();
  epd_4in2_v2_send_command(0x12); /* soft  reset */
  epd_4in2_v2_read_busy();

  epd_4in2_v2_send_command(0x21);
  epd_4in2_v2_send_data(0x40);
  epd_4in2_v2_send_data(0x00);

  epd_4in2_v2_send_command(0x3C);
  epd_4in2_v2_send_data(0x05);

  /* 1s refresh time */
  epd_4in2_v2_send_command(0x1A); /* Write to temperature register */
  epd_4in2_v2_send_data(0x5A);

  epd_4in2_v2_send_command(0x22); /* Load temperature value */
  epd_4in2_v2_send_data(0x91);
  epd_4in2_v2_send_command(0x20);
  epd_4in2_v2_read_busy();

  epd_4in2_v2_send_command(0x11); /* data  entry  mode */
  epd_4in2_v2_send_data(0x03);    /* X-mode */

  epd_4in2_v2_set_windows(0, 0, EPD_4IN2_V2_WIDTH - 1, EPD_4IN2_V2_HEIGHT - 1);

  epd_4in2_v2_set_cursor(0, 0);

  epd_4in2_v2_read_busy();
}

void epd_4in2_v2_init_4gray(void) {
  epd_4in2_v2_reset();

  epd_4in2_v2_send_command(0x12); /* SWRESET */
  epd_4in2_v2_read_busy();

  epd_4in2_v2_send_command(0x21);
  epd_4in2_v2_send_data(0x00);
  epd_4in2_v2_send_data(0x00);

  epd_4in2_v2_send_command(0x3C);
  epd_4in2_v2_send_data(0x03);

  epd_4in2_v2_send_command(0x0C); /* BTST */
  epd_4in2_v2_send_data(0x8B);    /* 8B */
  epd_4in2_v2_send_data(0x9C);    /* 9C */
  epd_4in2_v2_send_data(0xA4);    /* A4 */
  epd_4in2_v2_send_data(0x0F);    /* 0F */

  epd_4in2_v2_4gray_lut(); /* LUT */

  epd_4in2_v2_send_command(0x11); /* data  entry  mode */
  epd_4in2_v2_send_data(0x03);    /* X-mode */

  epd_4in2_v2_set_windows(0, 0, EPD_4IN2_V2_WIDTH - 1, EPD_4IN2_V2_HEIGHT - 1);

  epd_4in2_v2_set_cursor(0, 0);
}

/******************************************************************************
 * function :	Clear screen
 * parameter:
 *****************************************************************************/
void epd_4in2_v2_clear(void) {
  uint16_t width, height;

  width = (EPD_4IN2_V2_WIDTH % 8 == 0) ? (EPD_4IN2_V2_WIDTH / 8)
                                       : (EPD_4IN2_V2_WIDTH / 8 + 1);
  height = EPD_4IN2_V2_HEIGHT;

  epd_4in2_v2_send_command(0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(0xFF);
    }
  }

  epd_4in2_v2_send_command(0x26);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(0xFF);
    }
  }
  epd_4in2_v2_turn_on_display();
}

/******************************************************************************
 * function :	Sends the image buffer in RAM to e-Paper and displays
 * parameter:
 *****************************************************************************/
void epd_4in2_v2_display(uint8_t *image) {
  uint16_t width, height;

  width = (EPD_4IN2_V2_WIDTH % 8 == 0) ? (EPD_4IN2_V2_WIDTH / 8)
                                       : (EPD_4IN2_V2_WIDTH / 8 + 1);
  height = EPD_4IN2_V2_HEIGHT;

  epd_4in2_v2_set_windows(0, 0, EPD_4IN2_V2_WIDTH - 1, EPD_4IN2_V2_HEIGHT - 1);
  epd_4in2_v2_send_command(0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(image[i + j * width]);
    }
  }

  epd_4in2_v2_send_command(0x26);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(image[i + j * width]);
    }
  }
  epd_4in2_v2_turn_on_display();
}

/******************************************************************************
 * function :	Sends the image buffer in RAM to e-Paper and fast displays
 * parameter:
 *****************************************************************************/
void epd_4in2_v2_display_fast(uint8_t *image) {
  uint16_t width, height;

  width = (EPD_4IN2_V2_WIDTH % 8 == 0) ? (EPD_4IN2_V2_WIDTH / 8)
                                       : (EPD_4IN2_V2_WIDTH / 8 + 1);
  height = EPD_4IN2_V2_HEIGHT;

  epd_4in2_v2_set_windows(0, 0, EPD_4IN2_V2_WIDTH - 1, EPD_4IN2_V2_HEIGHT - 1);
  epd_4in2_v2_send_command(0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(image[i + j * width]);
    }
  }

  epd_4in2_v2_send_command(0x26);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(image[i + j * width]);
    }
  }
  epd_4in2_v2_turn_on_display_fast();
}

void epd_4in2_v2_display_4gray(uint8_t *image) {
  uint32_t i, j, k, m;
  uint8_t temp1, temp2, temp3;

  /****Color display description****
   *	white  gray2  gray1  black
   *	0x10|  01     01     00     00
   *	0x13|  01     00     01     00
   ********************************/
  epd_4in2_v2_send_command(0x24);
  /* EPD_4IN2_HEIGHT */
  /* EPD_4IN2_WIDTH */
  for (m = 0; m < EPD_4IN2_V2_HEIGHT; m++) {
    for (i = 0; i < EPD_4IN2_V2_WIDTH / 8; i++) {
      temp3 = 0;
      for (j = 0; j < 2; j++) {

        temp1 = image[(m * (EPD_4IN2_V2_WIDTH / 8) + i) * 2 + j];
        for (k = 0; k < 2; k++) {
          temp2 = temp1 & 0xC0;
          if (temp2 == 0xC0) {
            temp3 |= 0x01; /* white */
          } else if (temp2 == 0x00) {
            temp3 |= 0x00; /* black */
          } else if (temp2 == 0x80) {
            temp3 |= 0x00; /* gray1 */
          } else {         /* 0x40 */
            temp3 |= 0x01; /* gray2 */
          }
          temp3 <<= 1;

          temp1 <<= 2;
          temp2 = temp1 & 0xC0;
          if (temp2 == 0xC0) { /* white */
            temp3 |= 0x01;
          } else if (temp2 == 0x00) { /* black */
            temp3 |= 0x00;
          } else if (temp2 == 0x80) {
            temp3 |= 0x00; /* gray1 */
          } else {         /* 0x40 */
            temp3 |= 0x01; /* gray2 */
          }
          if (j != 1 || k != 1) {
            temp3 <<= 1;
          }

          temp1 <<= 2;
        }
      }
      epd_4in2_v2_send_data(temp3);
    }
  }
  /* new  data */
  epd_4in2_v2_send_command(0x26);
  for (m = 0; m < EPD_4IN2_V2_HEIGHT; m++) {
    for (i = 0; i < EPD_4IN2_V2_WIDTH / 8; i++) {
      temp3 = 0;
      for (j = 0; j < 2; j++) {
        temp1 = image[(m * (EPD_4IN2_V2_WIDTH / 8) + i) * 2 + j];
        for (k = 0; k < 2; k++) {
          temp2 = temp1 & 0xC0;
          if (temp2 == 0xC0) {
            temp3 |= 0x01; /* white */
          } else if (temp2 == 0x00) {
            temp3 |= 0x00; /* black */
          } else if (temp2 == 0x80) {
            temp3 |= 0x01; /* gray1 */
          } else {         /* 0x40 */
            temp3 |= 0x00; /* gray2 */
          }
          temp3 <<= 1;

          temp1 <<= 2;
          temp2 = temp1 & 0xC0;
          if (temp2 == 0xC0) { /* white */
            temp3 |= 0x01;
          } else if (temp2 == 0x00) { /* black */
            temp3 |= 0x00;
          } else if (temp2 == 0x80) {
            temp3 |= 0x01; /* gray1 */
          } else {         /* 0x40 */
            temp3 |= 0x00; /* gray2 */
          }
          if (j != 1 || k != 1) {
            temp3 <<= 1;
          }

          temp1 <<= 2;
        }
      }
      epd_4in2_v2_send_data(temp3);
    }
  }
  epd_4in2_v2_turn_on_display_4gray();
}

/* Send partial data for partial refresh */
void epd_4in2_v2_partial_display(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 uint8_t *image) {
  uint16_t width, height;

  width = (w % 8 == 0) ? (w / 8) : (w / 8 + 1);
  height = h;

  epd_4in2_v2_send_command(0x3C); /* BorderWavefrom, */
  epd_4in2_v2_send_data(0x80);

  epd_4in2_v2_send_command(0x21);
  epd_4in2_v2_send_data(0x00);
  epd_4in2_v2_send_data(0x00);

  epd_4in2_v2_send_command(0x3C);
  epd_4in2_v2_send_data(0x80);

  epd_4in2_v2_set_windows(x, y, x + w - 1, y + h - 1);
  epd_4in2_v2_set_cursor(x, y);

  epd_4in2_v2_send_command(0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(image[i + j * width]);
    }
  }

  epd_4in2_v2_turn_on_display_partial();
}

/******************************************************************************
 * function :	Enter sleep mode
 * parameter:
 *****************************************************************************/
void epd_4in2_v2_sleep(void) {
  epd_4in2_v2_send_command(0x10); /* DEEP_SLEEP */
  epd_4in2_v2_send_data(0x01);
  epd_delay_ms(200);
}

/******************************************************************************
 * function :	Sends the image buffer in RAM to e-Paper and fast displays
 * parameter:
 *****************************************************************************/
void epd_4in2_v2_write_display(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               uint8_t *image) {
  uint16_t width, height;

  width = (w % 8 == 0) ? (w / 8) : (w / 8 + 1);
  height = h;

  epd_4in2_v2_set_windows(x, y, x + w - 1, y + h - 1);
  epd_4in2_v2_set_cursor(x, y);
  epd_4in2_v2_send_command(0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(image[i + j * width]);
    }
  }

  epd_4in2_v2_send_command(0x26);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      epd_4in2_v2_send_data(image[i + j * width]);
    }
  }
}

/******************************************************************************
 * Utility functions
 *****************************************************************************/
void epd_4in2_v2_set_refresh_mode(epd_refresh_mode_t mode) {
  current_refresh_mode = mode;
}

epd_refresh_mode_t epd_4in2_v2_get_refresh_mode(void) {
  return current_refresh_mode;
}

bool epd_4in2_v2_is_busy(void) { return (epd_gpio_read(EPD_BUSY_PIN) == HIGH); }
