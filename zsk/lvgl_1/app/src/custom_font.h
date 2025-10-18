/*
 * Custom Font Rendering System
 * Uses cfbv_1016.h font data for direct text rendering
 */

#ifndef CUSTOM_FONT_H
#define CUSTOM_FONT_H

#include <stdint.h>
#include <zephyr/device.h>

// Font dimensions
#define FONT_WIDTH 10
#define FONT_HEIGHT 16
#define FONT_CHAR_WIDTH 2 // 2 bytes per row (16 bits = 10 pixels + 6 padding)

// Function declarations
int custom_font_render_text(const struct device *display, const char *text,
                            uint16_t x, uint16_t y);
int custom_font_clear_display(const struct device *display);
int custom_font_draw_char(const struct device *display, char c, uint16_t x,
                          uint16_t y);

#endif /* CUSTOM_FONT_H */
