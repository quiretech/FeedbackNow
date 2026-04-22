/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 *
 * SSD1683 E-Paper Display Driver - Zephyr Display API wrapper.
 *
 * This file only implements the Zephyr display API.  All state and the
 * heavy lifting live in ssd1683.c which owns the per-instance shadow
 * framebuffer, dirty-rect tracking, and refresh workqueue.
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ssd1683.h"

LOG_MODULE_REGISTER(ssd1683_display, CONFIG_DISPLAY_LOG_LEVEL);

#define DT_DRV_COMPAT solomon_ssd1683

/* Exported by ssd1683.c (not in the header because it is an internal hook
 * between the two TUs of this driver). */
extern void _ssd1683_stage_rect(const struct device *dev, uint16_t x,
                                uint16_t y, uint16_t w, uint16_t h,
                                const uint8_t *src, uint16_t src_pitch);
extern int _ssd1683_bringup(const struct device *dev);

/* ------------------------------------------------------------------------ */
/* Display API                                                              */
/* ------------------------------------------------------------------------ */

static int ssd1683_display_blanking_on(const struct device *dev)
{
    struct ssd1683_data *data = dev->data;
    int ret;

    (void)ssd1683_flush(dev, K_SECONDS(5));
    ret = ssd1683_power_off(dev);
    if (ret < 0) {
        LOG_ERR("blanking_on: power_off: %d", ret);
        return ret;
    }
    data->is_blanked = true;
    return 0;
}

static int ssd1683_display_blanking_off(const struct device *dev)
{
    struct ssd1683_data *data = dev->data;
    int ret;

    ret = ssd1683_power_on(dev);
    if (ret < 0) {
        LOG_ERR("blanking_off: power_on: %d", ret);
        return ret;
    }
    data->is_blanked = false;
    return 0;
}

static int ssd1683_display_write(const struct device *dev,
                                 const uint16_t x, const uint16_t y,
                                 const struct display_buffer_descriptor *desc,
                                 const void *buf)
{
    const struct ssd1683_config *cfg = dev->config;

    if (!desc || !buf) {
        return -EINVAL;
    }
    if (desc->width == 0 || desc->height == 0) {
        return -EINVAL;
    }
    if ((uint32_t)x + desc->width > cfg->width ||
        (uint32_t)y + desc->height > cfg->height) {
        LOG_ERR("write out of bounds: %u+%u x %u+%u vs %ux%u",
                x, desc->width, y, desc->height, cfg->width, cfg->height);
        return -EINVAL;
    }
    /* SSD1683 addresses RAM in bytes along X; require byte alignment. */
    if ((x % 8) != 0 || (desc->width % 8) != 0) {
        LOG_ERR("write area not byte aligned on X (x=%u w=%u)", x, desc->width);
        return -EINVAL;
    }

    /* desc->pitch is in bytes for monochrome MSB-first buffers. */
    uint16_t src_pitch = desc->pitch ? desc->pitch : (desc->width / 8);

    _ssd1683_stage_rect(dev, x, y, desc->width, desc->height,
                        (const uint8_t *)buf, src_pitch);
    return 0;
}

static void
ssd1683_display_get_capabilities(const struct device *dev,
                                 struct display_capabilities *capabilities)
{
    const struct ssd1683_config *cfg = dev->config;

    if (!capabilities) return;

    capabilities->x_resolution = cfg->width;
    capabilities->y_resolution = cfg->height;
    capabilities->supported_pixel_formats = PIXEL_FORMAT_MONO01;
    capabilities->current_pixel_format = PIXEL_FORMAT_MONO01;
    capabilities->screen_info = SCREEN_INFO_MONO_MSB_FIRST | SCREEN_INFO_EPD;
}

static int
ssd1683_display_set_pixel_format(const struct device *dev,
                                 const enum display_pixel_format pixel_format)
{
    ARG_UNUSED(dev);
    if (pixel_format != PIXEL_FORMAT_MONO01) {
        return -ENOTSUP;
    }
    return 0;
}

static int
ssd1683_display_set_orientation(const struct device *dev,
                                const enum display_orientation orientation)
{
    ARG_UNUSED(dev);
    if (orientation != DISPLAY_ORIENTATION_NORMAL) {
        return -ENOTSUP;
    }
    return 0;
}

static const struct display_driver_api ssd1683_display_api = {
    .blanking_on      = ssd1683_display_blanking_on,
    .blanking_off     = ssd1683_display_blanking_off,
    .write            = ssd1683_display_write,
    .get_capabilities = ssd1683_display_get_capabilities,
    .set_pixel_format = ssd1683_display_set_pixel_format,
    .set_orientation  = ssd1683_display_set_orientation,
};

static int ssd1683_display_init(const struct device *dev)
{
    LOG_INF("init %s", dev->name);
    return _ssd1683_bringup(dev);
}

/* ------------------------------------------------------------------------ */
/* Per-node instantiation                                                   */
/* ------------------------------------------------------------------------ */

#define SSD1683_SHADOW_BYTES(n) (DT_PROP(n, width) * DT_PROP(n, height) / 8)

#define SSD1683_DISPLAY_INIT(n)                                                  \
    static uint8_t ssd1683_shadow_##n[SSD1683_SHADOW_BYTES(n)];                  \
    K_KERNEL_STACK_DEFINE(ssd1683_wq_stack_##n,                                  \
                          CONFIG_SSD1683_WORKQUEUE_STACK_SIZE);                  \
    static struct k_work_q ssd1683_wq_##n;                                       \
    static struct ssd1683_data ssd1683_data_##n;                                 \
    static const struct ssd1683_config ssd1683_config_##n = {                    \
        .bus  = SPI_DT_SPEC_GET(n,                                               \
                                SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB |          \
                                    SPI_WORD_SET(8),                             \
                                0),                                              \
        .dc   = GPIO_DT_SPEC_GET(n, dc_gpios),                                   \
        .rst  = GPIO_DT_SPEC_GET(n, reset_gpios),                                \
        .busy = GPIO_DT_SPEC_GET(n, busy_gpios),                                 \
        .width  = DT_PROP(n, width),                                             \
        .height = DT_PROP(n, height),                                            \
        .fast_mode = DT_PROP(n, fast_mode),                                      \
        .shadow_fb = ssd1683_shadow_##n,                                         \
        .shadow_fb_size = sizeof(ssd1683_shadow_##n),                            \
        .workq = &ssd1683_wq_##n,                                                \
        .workq_stack = ssd1683_wq_stack_##n,                                     \
        .workq_stack_size = K_KERNEL_STACK_SIZEOF(ssd1683_wq_stack_##n),         \
    };                                                                           \
    DEVICE_DT_DEFINE(n, ssd1683_display_init, NULL, &ssd1683_data_##n,           \
                     &ssd1683_config_##n, POST_KERNEL,                           \
                     CONFIG_DISPLAY_INIT_PRIORITY, &ssd1683_display_api);

DT_FOREACH_STATUS_OKAY(solomon_ssd1683, SSD1683_DISPLAY_INIT)
