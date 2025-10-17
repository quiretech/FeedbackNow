/*
 * Direct Partial Refresh Test Header
 */

#ifndef TEST_PARTIAL_H
#define TEST_PARTIAL_H

#include <zephyr/device.h>

/**
 * Test partial refresh by directly calling display_write with a small region
 * This bypasses LVGL to test the hardware driver's partial refresh capability
 *
 * @param display Pointer to the display device
 */
void test_partial_refresh_direct(const struct device *display);

/**
 * Test CFB (Character Framebuffer) text rendering
 * Demonstrates how to use Zephyr's CFB to draw text on the display
 *
 * @param display Pointer to the display device
 */
void test_cfb_text(const struct device *display);

/**
 * Test CFB with multiple fonts
 * Demonstrates switching between different CFB fonts
 *
 * @param display Pointer to the display device
 */
void test_cfb_multiple_fonts(const struct device *display);

#endif /* TEST_PARTIAL_H */
