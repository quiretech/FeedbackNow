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

#endif /* TEST_PARTIAL_H */
