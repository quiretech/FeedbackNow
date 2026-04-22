/**
 * @file main.c
 * @brief SSD1683 Partial Refresh Demo
 *
 * Paints a static title + separator once with a full refresh, then updates
 * an HH:MM:SS timestamp every second using SSD1683 hardware partial refresh.
 * A full refresh is automatically triggered by the driver every
 * CONFIG_SSD1683_PARTIAL_MAX_BEFORE_FULL cycles to clear ghosting.
 */

#include <lvgl.h>
#include <stdio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "power_ctrl.h"
#include "ssd1683.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

LV_FONT_DECLARE(roboto_28);
LV_FONT_DECLARE(roboto_36);
LV_FONT_DECLARE(roboto_bold_42);

/* Display geometry must match the DT overlay (solomon,ssd1683 width/height).
 * We hard-code here because LVGL needs the buffer sizes at compile time. */
#define EPD_W            400
#define EPD_H            300
#define EPD_STRIDE_BYTES ((EPD_W + 7) / 8)               /* 50 bytes per row */
#define EPD_LV_BUF_BYTES ((EPD_STRIDE_BYTES * EPD_H) + 8) /* +8: I1 palette  */

/* Two full-screen LVGL buffers for DIRECT render mode. Why DIRECT and not
 * PARTIAL: LVGL 9.3's PARTIAL + LV_COLOR_FORMAT_I1 path delivers chunks
 * with broken coordinates / offsets (stride is display-wide but pixels
 * are placed at unpredictable y inside the chunk buffer), which produces
 * the "TV-static bands + thin band of correct pixels at the bottom"
 * symptom we just hit. The known-good workaround used by
 * fb_now/v1.1/display_manager.c (line 1144 comment) is DIRECT mode with
 * full-screen buffers, which we mirror here. */
static uint8_t __aligned(4) lv_fb_a[EPD_LV_BUF_BYTES];
static uint8_t __aligned(4) lv_fb_b[EPD_LV_BUF_BYTES];

static const struct device *s_display_dev;

/**
 * @brief Custom LVGL flush callback for the SSD1683 mono EPD (DIRECT mode).
 *
 * In DIRECT mode `px_map` is the full-screen LVGL framebuffer (with the
 * I1 palette in the first 8 bytes); `area` is the dirty sub-rectangle
 * within it. Each row is `EPD_STRIDE_BYTES` wide.
 *
 * We DON'T have to invert: LVGL I1 packs `1 = palette[1] = white` and
 * `0 = palette[0] = black`, which matches SSD1683 MONO01 RAM (1=white,
 * 0=black). Inverting would flip the screen.
 *
 * To keep the driver's `_ssd1683_stage_rect` math simple
 * (`src_row = src + row*pitch`) we pre-offset `src` to point at
 * (x1, y1) inside the LVGL framebuffer. Then row r in the source is
 * exactly row (y1+r) of the LVGL FB, columns x1..x1+w-1.
 */
static void ssd1683_lv_flush_cb(lv_display_t *disp, const lv_area_t *area,
                                uint8_t *px_map)
{
    int32_t x1 = MAX(area->x1, 0);
    int32_t y1 = MAX(area->y1, 0);
    int32_t x2 = MIN(area->x2, EPD_W - 1);
    int32_t y2 = MIN(area->y2, EPD_H - 1);
    if (x2 < x1 || y2 < y1) {
        lv_display_flush_ready(disp);
        return;
    }

    /* Round x to byte boundaries - the SSD1683 addresses RAM in bytes
     * along X, and our shadow FB is laid out the same way. The Zephyr
     * lvgl_rounder_cb_mono usually does this, but it relies on
     * SCREEN_INFO_MONO_MSB_FIRST being seen at rounder time and we
     * shouldn't trust it given the LVGL-mono path is full of edge cases. */
    x1 &= ~0x7;
    x2 |=  0x7;
    if (x2 > EPD_W - 1) x2 = EPD_W - 1;

    const uint16_t w = (uint16_t)(x2 - x1 + 1);
    const uint16_t h = (uint16_t)(y2 - y1 + 1);

    /* Skip the 8-byte palette, then offset to (x1, y1) inside the
     * full-screen FB so the driver reads contiguous rows. */
    const uint8_t *fb  = px_map + 8;
    const uint8_t *src = fb + (uint32_t)y1 * EPD_STRIDE_BYTES + (x1 / 8);

    struct display_buffer_descriptor desc = {
        .buf_size = (uint32_t)EPD_STRIDE_BYTES * h, /* informational */
        .width    = w,
        .pitch    = EPD_STRIDE_BYTES,               /* full row stride */
        .height   = h,
        .frame_incomplete = !lv_display_flush_is_last(disp),
    };
    display_write(s_display_dev, (uint16_t)x1, (uint16_t)y1, &desc, src);
    lv_display_flush_ready(disp);
}

static void build_static_ui(lv_obj_t *screen, lv_obj_t **time_label_out)
{
    /* White background (MONO01: 1=white). */
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    /* "Uptime:" caption centered vertically */
    static lv_style_t caption_style;
    lv_style_init(&caption_style);
    lv_style_set_text_font(&caption_style, &roboto_28);
    lv_style_set_text_color(&caption_style, lv_color_black());

    lv_obj_t *caption = lv_label_create(screen);
    lv_obj_add_style(caption, &caption_style, 0);
    lv_label_set_text(caption, "Uptime:");
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -40);

    /* Big HH:MM:SS label - the only thing we repaint every second. */
    static lv_style_t time_style;
    lv_style_init(&time_style);
    lv_style_set_text_font(&time_style, &roboto_bold_42);
    lv_style_set_text_color(&time_style, lv_color_black());

    lv_obj_t *t = lv_label_create(screen);
    lv_obj_add_style(t, &time_style, 0);
    lv_label_set_text(t, "00:00:00");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 20);

    *time_label_out = t;
}

static void format_hms(char *out, size_t out_sz, uint32_t total_seconds)
{
    uint32_t h = (total_seconds / 3600) % 100;
    uint32_t m = (total_seconds / 60) % 60;
    uint32_t s = total_seconds % 60;
    snprintf(out, out_sz, "%02u:%02u:%02u", h, m, s);
}

int main(void)
{
    const struct device *display;
    lv_obj_t *time_label = NULL;
    int ret;

    LOG_INF("SSD1683 partial-refresh demo");

    ret = power_ctrl_init();
    if (ret != 0) {
        LOG_ERR("power_ctrl_init: %d", ret);
        return ret;
    }
    power_ctrl_set(POWER_EN_3V3, true);
    power_ctrl_set(POWER_EN_1V8, true);
    power_ctrl_set(POWER_EN_3V3A, true);
    power_ctrl_set(POWER_EN_3V6, true);
    k_msleep(10);

    display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display)) {
        LOG_ERR("display not ready");
        return -ENODEV;
    }
    s_display_dev = display;

    /* lvgl_init() already created the default display at POST_KERNEL/
     * APPLICATION with: VDB-sized PARTIAL buffers, LVGL's stock
     * lvgl_flush_cb_mono (which both off-by-8s past its conversion buffer
     * AND assumes a chunk layout that LVGL 9.3 does not actually deliver
     * for I1, see the discussion at the top of ssd1683_lv_flush_cb).
     * We rip all of that out and replace it with:
     *   - LV_COLOR_FORMAT_I1 (explicit; the default with COLOR_DEPTH_1=y
     *     is already I1 but be defensive)
     *   - two full-screen buffers in DIRECT render mode
     *   - our own flush_cb
     * The Zephyr-allocated VDB / mono-conversion buffer become unused
     * memory but stay reachable (no UAF), and the rounder cb registered
     * by lvgl_init() is still installed and harmless: it byte-aligns x,
     * which we also do defensively in flush_cb. */
    lv_display_t *def_disp = lv_display_get_default();
    if (def_disp == NULL) {
        LOG_ERR("LVGL default display missing - lvgl_init failed?");
        return -ENODEV;
    }
    lv_display_set_color_format(def_disp, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers_with_stride(def_disp, lv_fb_a, lv_fb_b,
                                       EPD_LV_BUF_BYTES, EPD_STRIDE_BYTES,
                                       LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(def_disp, ssd1683_lv_flush_cb);
    LOG_INF("LVGL: DIRECT mode, %ux%u I1, two %u-byte FBs",
            EPD_W, EPD_H, (unsigned)EPD_LV_BUF_BYTES);

    build_static_ui(lv_scr_act(), &time_label);

    display_blanking_off(display);

    /* First paint: let LVGL render, then force a full refresh so the initial
     * screen is sharp and RAM-B gets synchronised. */
    lv_task_handler();
    ssd1683_force_full_refresh(display);
    ret = ssd1683_flush(display, K_SECONDS(5));
    if (ret < 0) {
        LOG_WRN("initial flush: %d", ret);
    }

    LOG_INF("entering 1 Hz partial-refresh loop");

    int64_t t_epoch = k_uptime_get();
    char buf[16];
    while (1) {
        uint32_t seconds = (k_uptime_get() - t_epoch) / 1000;
        format_hms(buf, sizeof(buf), seconds);
        lv_label_set_text(time_label, buf);

        int64_t t_tick = k_uptime_get();
        lv_task_handler();
        int64_t t_staged = k_uptime_get();

        ret = ssd1683_flush(display, K_SECONDS(3));
        int64_t t_done = k_uptime_get();

        LOG_INF("tick %s: stage=%lldms refresh=%lldms ret=%d",
                buf,
                t_staged - t_tick,
                t_done - t_staged,
                ret);

        int64_t spent = k_uptime_get() - t_tick;
        int64_t remaining = 1000 - spent;
        if (remaining > 0) {
            k_msleep(remaining);
        }
    }
    return 0;
}
