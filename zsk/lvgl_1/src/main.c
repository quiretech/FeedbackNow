#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fonts.h"
#include "paint.h"
#include "ssd1683.h"

LOG_MODULE_REGISTER(epd_main, LOG_LEVEL_INF);
#define LED0_NODE DT_ALIAS(led0)
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define ARDUINO_SPI DT_NODELABEL(arduino_spi)
#define EPD_SPI_DEVICE DT_NODELABEL(epd_spi_device)
#define EPD_SPI_CS_DT_SPEC                                                     \
  SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(epd_spi_device))

// SSD1683 display configuration
static const struct ssd1683_config epd_cfg = {
    .spi_dev = DEVICE_DT_GET(ARDUINO_SPI),
    .spi_cfg =
        {
            .operation = SPI_WORD_SET(8) | SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
                         SPI_MODE_CPOL | SPI_MODE_CPHA,
            .frequency = 4000000,
            .slave = 0,
            .cs = {.gpio = EPD_SPI_CS_DT_SPEC, .delay = 0},
        },
    .dc = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_dc_gpios),
    .rst = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_rst_gpios),
    .busy = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_busy_gpios),
    .width = 400,
    .height = 300,
};

int main(void) {
  int ret;

  // Initialize display
  ret = ssd1683_init(&epd_cfg);
  if (ret < 0) {
    return ret;
  }

  ssd1683_clear(&epd_cfg);
  ssd1683_refresh(&epd_cfg);

  // Buffers for paint
  static uint8_t wb_buffer[15000];
  static uint8_t rw_buffer[15000];

  paint_obj_t paint = {
      .width = 400,
      .height = 300,
      .direction = PAINT_DIRECTION_0,
      .scanmode = PAINT_SCAN_MODE_1,
      .wb_buffer = wb_buffer,
      .rw_buffer = rw_buffer,
  };
  paint_Init(&paint);

  // List of fonts and label strings
  extern sFONT Font20;
  extern sFONT Font24;
  struct {
    sFONT *font;
    const char *label;
  } font_cards[] = {
      {&Font20, "Font 20"},
      {&Font24, "Font 24"},
  };

  // Setup basic ascii for card
  const char start_char = 32; // ' '
  const char end_char = 127;  // last printable (not including DEL)
  const int max_chars = end_char - start_char;

  int margin = 8;
  int current_y = margin;
  int card_spacing = 6;

  paint_Fill(BLACK);

  for (unsigned f = 0; f < sizeof(font_cards) / sizeof(font_cards[0]); ++f) {
    sFONT *font = font_cards[f].font;
    const char *label = font_cards[f].label;

    // Draw font label at card top
    paint_rect_t label_bg = {0, current_y, paint.width, font->Height};
    paint_FillRect(WHITE, &label_bg);
    for (int l = 0; label[l]; ++l) {
      paint_DrawFont(label[l], font, BLACK, margin + l * font->Width,
                     current_y);
    }

    current_y += font->Height + 2;

    // Compute how many glyphs per row
    int chars_per_row = (paint.width - margin * 2) / font->Width;
    if (chars_per_row > max_chars)
      chars_per_row = max_chars;
    int rows = (max_chars + chars_per_row - 1) / chars_per_row;

    char c = start_char;
    for (int row = 0; row < rows && c < end_char; ++row) {
      int x = margin;
      int y = current_y + row * font->Height;
      for (int ch = 0; ch < chars_per_row && c < end_char; ++ch, ++c) {
        paint_DrawFont(c, font, WHITE, x, y);
        x += font->Width;
      }
    }
    current_y += rows * font->Height + card_spacing;
    if (current_y + font->Height > paint.height)
      break; // stop if out of screen
  }

  // Draw frame
  paint_rect_t border = {0, 0, paint.width, paint.height};
  paint_DrawRect(WHITE, &border);

  ssd1683_flush_from_paint(&epd_cfg, wb_buffer, rw_buffer);
  ssd1683_refresh(&epd_cfg);

  return 0;
}
