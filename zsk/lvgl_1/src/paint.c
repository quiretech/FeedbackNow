/**
 ******************************************************************************
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Filename : jfpaint.c
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include <fonts.h>
#include <paint.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

/* Macro ---------------------------------------------------------------------*/
LOG_MODULE_REGISTER(paint);

// 3-color mode for SSD1683 display (always enabled)
#define CONFIG_PAINT_WB_ON 1
#define CONFIG_PAINT_RW_ON 1
#define CONFIG_PAINT_COLORS 3
#define CONFIG_PAINT_MSB_FIRST 1

// 3-color mode definitions (always enabled)
#define WB_BLACK_BYTE (0xff)
#define WB_WHITE_BYTE (0x00)
#define wb_black_mask(byte, mask) (byte |= mask)
#define wb_white_mask(byte, mask) (byte &= ~mask)

#define RW_RED_BYTE (0xff)
#define RW_WHITE_BYTE (0x00)
#define rw_red_mask(byte, mask) (byte |= mask)
#define rw_white_mask(byte, mask) (byte &= ~mask)

/* Private typedef -----------------------------------------------------------*/

/* Private function prototypes------------------------------------------------*/
static void setcolor(uint32_t index, paint_color_t color, uint8_t mask);
static void drawHline(paint_color_t color, uint32_t x, uint32_t y, uint32_t w);

/* Global variables ----------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static paint_obj_t *p_Paint;

/* Macro ---------------------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/
void paint_Init(paint_obj_t *paint_obj) {
  p_Paint = paint_obj;
  switch (p_Paint->direction) {
  case PAINT_DIRECTION_90:
  case PAINT_DIRECTION_270:
    p_Paint->x_end = p_Paint->height;
    p_Paint->y_end = p_Paint->width;
    break;
  default:
    p_Paint->direction = PAINT_DIRECTION_0;
  case PAINT_DIRECTION_0:
  case PAINT_DIRECTION_180:
    p_Paint->x_end = p_Paint->width;
    p_Paint->y_end = p_Paint->height;
    break;
  }

  p_Paint->buffer_size = p_Paint->height * (p_Paint->width / 8);
}

void paint_SetDirection(paint_direction_t direction) {
  p_Paint->direction = direction;
  switch (p_Paint->direction) {
  case PAINT_DIRECTION_90:
  case PAINT_DIRECTION_270:
    p_Paint->x_end = p_Paint->height;
    p_Paint->y_end = p_Paint->width;
    break;
  default:
    p_Paint->direction = PAINT_DIRECTION_0;
  case PAINT_DIRECTION_0:
  case PAINT_DIRECTION_180:
    p_Paint->x_end = p_Paint->width;
    p_Paint->y_end = p_Paint->height;
    break;
  }
}

void paint_Fill(paint_color_t color) {
  uint8_t wb, rw;
  switch (color) {
  case RED:
    wb = WB_WHITE_BYTE;
    rw = RW_RED_BYTE;
    break;
  case BLACK:
    wb = WB_BLACK_BYTE;
    rw = RW_WHITE_BYTE;
    break;
  case WHITE:
  default:
    wb = WB_WHITE_BYTE;
    rw = RW_WHITE_BYTE;
    break;
  }
  memset((void *)p_Paint->wb_buffer, wb, p_Paint->buffer_size);
  memset((void *)p_Paint->rw_buffer, rw, p_Paint->buffer_size);
}

void paint_DrawPoint(paint_color_t color, uint32_t x, uint32_t y) {
  uint32_t temp;
  uint8_t mask;

  if ((x >= p_Paint->x_end) || (y >= p_Paint->y_end)) {
    return;
  }

  switch (p_Paint->direction) {
  case PAINT_DIRECTION_90:
    temp = x;
    x = p_Paint->width - y - 1;
    y = temp;
    break;
  case PAINT_DIRECTION_180:
    x = p_Paint->width - x - 1;
    y = p_Paint->height - y - 1;
    break;
  case PAINT_DIRECTION_270:
    temp = x;
    x = y;
    y = p_Paint->height - temp - 1;
    break;
  case PAINT_DIRECTION_0:
  default:
    break;
  }
  if (p_Paint->scanmode == PAINT_SCAN_MODE_2) {
    y = p_Paint->height - y - 1;
  }
  mask = 1 << (7 - x % 8);
  setcolor(y * (p_Paint->width / 8) + x / 8, color, mask);
}

void paint_FillRect(paint_color_t color, paint_rect_t *rect) {
  if ((rect->x + rect->width > p_Paint->x_end) ||
      (rect->y + rect->height > p_Paint->y_end)) {
    return;
  }
  uint32_t x, y, w, h, i;
  switch (p_Paint->direction) {
  case PAINT_DIRECTION_90:
    x = p_Paint->width - (rect->y + rect->width);
    y = rect->x;
    w = rect->height;
    h = rect->width;
    break;
  case PAINT_DIRECTION_180:
    x = p_Paint->width - (rect->x + rect->width);
    y = p_Paint->height - (rect->y + rect->height);
    w = rect->width;
    h = rect->height;
    break;
  case PAINT_DIRECTION_270:
    x = rect->y;
    y = p_Paint->height - (rect->x + rect->width);
    w = rect->height;
    h = rect->width;
    break;
  case PAINT_DIRECTION_0:
  default:
    x = rect->x;
    y = rect->y;
    w = rect->width;
    h = rect->height;
    break;
  }

  for (i = y; i < y + h; i++) {
    drawHline(color, x, i, w);
  }
}

void paint_DrawFont(char c, sFONT *font, paint_color_t color, int x, int y) {
  if ((x + font->Width > p_Paint->x_end) ||
      (y + font->Height > p_Paint->y_end)) {
    return;
  }

  uint32_t char_offset;
  int i, j;
  // Calculate bytes per row based on actual data structure
  // The font data is padded to byte boundaries, so we need to figure out the
  // actual width
  int bytes_per_row;

  // Based on the actual font data structure:
  // Font8: 1 byte per row (8 bits)
  // Font12: 2 bytes per row (16 bits)
  // Font16: 2 bytes per row (16 bits)
  // Font20: 3 bytes per row (24 bits)
  // Font24: 3 bytes per row (24 bits)

  // Based on actual font data structure:
  // Font8: 7 pixels → 1 byte per row
  // Font12: 11 pixels → 2 bytes per row
  // Font16: 14 pixels → 2 bytes per row
  // Font20: 18 pixels → 3 bytes per row
  // Font24: 22 pixels → 3 bytes per row

  // Specific handling for each font based on actual data structure
  if (font->Width == 7) { // Font8
    bytes_per_row = 1;
  } else if (font->Width == 11) { // Font12
    bytes_per_row = 2;
  } else if (font->Width == 14) { // Font16
    bytes_per_row = 2;
  } else if (font->Width == 18) { // Font20
    bytes_per_row = 3;
  } else if (font->Width == 22) { // Font24
    bytes_per_row = 3;
  } else {
    bytes_per_row = (font->Width + 7) / 8;
  }

  c = c - ' ';
  char_offset = c * font->Height * bytes_per_row;

  // Remove bounds checking for now to see if that's the issue
  // uint32_t max_offset = 95 * font->Height * bytes_per_row;
  // if (char_offset >= max_offset) {
  //   return; // Character not in font table
  // }

  // // Additional safety check
  // if (char_offset + (font->Height * bytes_per_row) > max_offset) {
  //   return; // Would go out of bounds
  // }

  const uint8_t *ptr = &font->table[char_offset];

  // For debugging: draw a simple pattern to see if we can access the font data
  // This will help us determine if the issue is with data access or drawing
  for (j = 0; j < font->Height; j++) {
    for (i = 0; i < font->Width; i++) {
      // Try to read the font data and draw based on it
      if (*ptr & (0x80 >> (i % 8))) {
        paint_DrawPoint(color, x + i, y + j);
      }
      if (i % 8 == 7) {
        ptr++;
      }
    }
    if (font->Width % 8 != 0) {
      ptr++;
    }
  }
}

void paint_DrawString(const char *s, sFONT *font, paint_color_t color, int x,
                      int y) {
  uint32_t s_index = 0;
  uint32_t s_length = strlen(s);
  do {
    if (x + font->Width <= p_Paint->x_end) {
      paint_DrawFont(s[s_index], font, color, x, y);
      x += font->Width;
      s_index++;
    } else {
      break;
    }
  } while (s_index < s_length);
}

/* Overrride functions -------------------------------------------------------*/

/* Private Functions ---------------------------------------------------------*/
static void setcolor(uint32_t index, paint_color_t color, uint8_t mask) {
  switch (color) {
  case BLACK:
    wb_black_mask(p_Paint->wb_buffer[index], mask);
    rw_white_mask(p_Paint->rw_buffer[index], mask);
    break;
  case RED:
    wb_white_mask(p_Paint->wb_buffer[index], mask);
    rw_red_mask(p_Paint->rw_buffer[index], mask);
    break;
  case WHITE:
  default:
    wb_white_mask(p_Paint->wb_buffer[index], mask);
    rw_white_mask(p_Paint->rw_buffer[index], mask);
    break;
  }
}

static void drawHline(paint_color_t color, uint32_t x, uint32_t y, uint32_t w) {
  uint32_t i;
  uint8_t mask;

  if (p_Paint->scanmode == PAINT_SCAN_MODE_2) {
    y = p_Paint->height - y - 1;
  }

  for (i = x; i < 8 + (x & (~0x7)); i++) {
    if (i >= x + w) {
      return;
    }
    mask = 1 << (7 - i % 8);
    setcolor(y * (p_Paint->width / 8) + x / 8, color, mask);
  }

  while (i + 8 < x + w) {
    setcolor(y * (p_Paint->width / 8) + i / 8, color, 0xff);
    i += 8;
  }

  while (i < x + w) {
    mask = 1 << (7 - i % 8);
    setcolor(y * (p_Paint->width / 8) + i / 8, color, mask);
    i++;
  }
}

/* Macro ---------------------------------------------------------------------*/

/* End of File */
