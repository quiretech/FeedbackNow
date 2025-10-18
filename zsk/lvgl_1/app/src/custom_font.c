/*
 * Custom Font Rendering Implementation
 * Direct font rendering using cfbv_1016.h font data
 */

#include "custom_font.h"
#include "fonts/cfbv_1016.h"
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(custom_font, LOG_LEVEL_INF);

// Helper function to get font data for a character
static const uint8_t *get_font_data(char c) {
  // Convert character to font index (ASCII 32-126 maps to 0-94)
  if (c < 32 || c > 126) {
    c = 32; // Use space for invalid characters
  }
  return cfb_font_custom_1016[c - 32];
}

// Helper function to set a pixel in a buffer
static void set_pixel(uint8_t *buffer, int width_bytes, int x, int y,
                      bool black) {
  if (x < 0 || y < 0)
    return;

  int byte_index = (y * width_bytes) + (x / 8);
  int bit_index = 7 - (x % 8);

  if (black) {
    buffer[byte_index] &= ~(1 << bit_index); // Clear bit = black pixel
  } else {
    buffer[byte_index] |= (1 << bit_index); // Set bit = white pixel
  }
}

// Draw a single character
int custom_font_draw_char(const struct device *display, char c, uint16_t x,
                          uint16_t y) {
  const uint8_t *font_data = get_font_data(c);

  // Get display capabilities
  struct display_capabilities caps;
  display_get_capabilities(display, &caps);

  int width_bytes = (caps.x_resolution + 7) / 8;
  int total_bytes = width_bytes * caps.y_resolution;

  // Allocate buffer for this character
  uint8_t *char_buffer = k_malloc(total_bytes);
  if (!char_buffer) {
    LOG_ERR("Failed to allocate character buffer");
    return -ENOMEM;
  }

  // Clear buffer to white (0xFF = white for EPD)
  memset(char_buffer, 0xFF, total_bytes);

  // Render character
  for (int row = 0; row < FONT_HEIGHT; row++) {
    // Get the two bytes for this row (little-endian)
    uint16_t row_data = font_data[row * FONT_CHAR_WIDTH] |
                        (font_data[row * FONT_CHAR_WIDTH + 1] << 8);

    for (int col = 0; col < FONT_WIDTH; col++) {
      // Check if pixel should be black (bit is set)
      // Font data is MSB first, so we check from left to right
      if (row_data & (0x8000 >> col)) {
        set_pixel(char_buffer, width_bytes, x + col, y + row, true);
      }
    }
  }

  // Create display buffer descriptor
  struct display_buffer_descriptor desc = {
      .buf_size = total_bytes,
      .width = caps.x_resolution,
      .height = caps.y_resolution,
      .pitch = width_bytes,
  };

  // Write to display
  int ret = display_write(display, 0, 0, &desc, char_buffer);
  if (ret < 0) {
    LOG_ERR("Display write failed: %d", ret);
  }

  k_free(char_buffer);
  return ret;
}

// Clear the display to white
int custom_font_clear_display(const struct device *display) {
  struct display_capabilities caps;
  display_get_capabilities(display, &caps);

  int width_bytes = (caps.x_resolution + 7) / 8;
  int total_bytes = width_bytes * caps.y_resolution;

  // Allocate white buffer
  uint8_t *white_buffer = k_malloc(total_bytes);
  if (!white_buffer) {
    LOG_ERR("Failed to allocate white buffer");
    return -ENOMEM;
  }

  // Fill with white (0xFF = white for EPD)
  memset(white_buffer, 0xFF, total_bytes);

  struct display_buffer_descriptor desc = {
      .buf_size = total_bytes,
      .width = caps.x_resolution,
      .height = caps.y_resolution,
      .pitch = width_bytes,
  };

  int ret = display_write(display, 0, 0, &desc, white_buffer);
  k_free(white_buffer);

  return ret;
}

// Render text string
int custom_font_render_text(const struct device *display, const char *text,
                            uint16_t x, uint16_t y) {
  LOG_INF("Rendering text: '%s' at (%d, %d)", text, x, y);

  // Clear display first
  int ret = custom_font_clear_display(display);
  if (ret < 0) {
    LOG_ERR("Failed to clear display: %d", ret);
    return ret;
  }

  // Render each character
  uint16_t current_x = x;
  for (int i = 0; text[i] != '\0'; i++) {
    ret = custom_font_draw_char(display, text[i], current_x, y);
    if (ret < 0) {
      LOG_ERR("Failed to draw character '%c': %d", text[i], ret);
      return ret;
    }
    current_x += FONT_WIDTH + 1; // Add 1 pixel spacing between characters
  }

  LOG_INF("Text rendered successfully");
  return 0;
}
