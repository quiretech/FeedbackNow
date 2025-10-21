/*****************************************************************************
 * |File	:	epd_4in2_v2.h
 * |Author	:	Waveshare team (adapted for standalone use)
 * |Function	:	4.2inch e-paper V2 Driver
 * |Info	:	Standalone driver for nRF52840DK
 *----------------
 * |This version:	V2.0
 * |Date	:	2025-01-08
 * |Info	:	Extracted and adapted from Nordic ESL project
 ****************************************************************************/
#ifndef _EPD_4IN2_V2_H_
#define _EPD_4IN2_V2_H_

#include <stdbool.h>
#include <stdint.h>

/* Display resolution */
#define EPD_4IN2_V2_WIDTH 400
#define EPD_4IN2_V2_HEIGHT 300

/* Refresh modes */
typedef enum {
  EPD_REFRESH_FULL = 0,    // Full refresh (high quality)
  EPD_REFRESH_PARTIAL = 1, // Partial refresh (faster)
  EPD_REFRESH_FAST = 2     // Fast refresh (fastest)
} epd_refresh_mode_t;

/* Color definitions */
typedef enum {
  EPD_COLOR_WHITE = 0,
  EPD_COLOR_BLACK = 1,
  EPD_COLOR_LIGHT_GRAY = 2,
  EPD_COLOR_DARK_GRAY = 3
} epd_color_t;

/* Driver API Functions */
void epd_4in2_v2_init(void);
void epd_4in2_v2_init_fast(void);
void epd_4in2_v2_init_4gray(void);
void epd_4in2_v2_clear(void);
void epd_4in2_v2_display(uint8_t *image);
void epd_4in2_v2_display_fast(uint8_t *image);
void epd_4in2_v2_display_4gray(uint8_t *image);
void epd_4in2_v2_partial_display(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 uint8_t *image);
void epd_4in2_v2_sleep(void);
void epd_4in2_v2_write_display(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               uint8_t *image);
void epd_4in2_v2_turn_on_display_fast(void);

/* Utility functions */
void epd_4in2_v2_set_refresh_mode(epd_refresh_mode_t mode);
epd_refresh_mode_t epd_4in2_v2_get_refresh_mode(void);
bool epd_4in2_v2_is_busy(void);

#endif
