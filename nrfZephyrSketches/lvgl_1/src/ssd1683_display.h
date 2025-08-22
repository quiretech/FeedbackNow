#pragma once

#include "ssd1683.h"
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

int ssd1683_display_init(const struct device *dev);
