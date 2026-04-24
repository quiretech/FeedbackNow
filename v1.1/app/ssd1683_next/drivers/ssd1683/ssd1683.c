/**
 * @file ssd1683.c
 * @brief SSD1683 E-Paper Display Driver - low level
 *
 * Layout:
 *   - Section 1: SPI / GPIO low-level helpers.
 *   - Section 2: Init script, address-range helpers.
 *   - Section 3: Shadow framebuffer push + refresh sequences.
 *   - Section 4: Workqueue handler with partial/full decision.
 *   - Section 5: Public API.
 */

#include "ssd1683.h"
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ssd1683, LOG_LEVEL_INF);

/* ------------------------------------------------------------------------ */
/* Section 1: low-level SPI / GPIO                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Poll the BUSY line until it deasserts or @p timeout elapses.
 *
 * @p phase is a short string (e.g. "partial", "full", "reset") emitted in
 * the error log so we can tell which step of the update sequence hung.
 * That is crucial because the SSD1683 has several independent paths that
 * can each stall BUSY, and the recovery path in the caller is the same
 * regardless, but the diagnostic value is enormous.
 */
static int _wait_busy(const struct ssd1683_config *cfg, k_timeout_t timeout,
                      const char *phase)
{
    int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
    while (gpio_pin_get_dt(&cfg->busy)) {
        if (k_uptime_get() > deadline) {
            LOG_ERR("BUSY timeout in %s", phase);
            return -ETIMEDOUT;
        }
        k_msleep(1);
    }
    return 0;
}

/**
 * @brief Single-shot tx helper.  Sends a command byte and, optionally,
 *        a data payload.  Both are delivered in exactly one SPI transfer
 *        each, with a single DC toggle.
 */
static int _tx(const struct ssd1683_config *cfg, uint8_t cmd,
               const void *data, size_t len)
{
    int ret;

    ret = gpio_pin_set_dt(&cfg->dc, 0);
    if (ret < 0) {
        return ret;
    }

    struct spi_buf cbuf = { .buf = &cmd, .len = 1 };
    struct spi_buf_set ctx = { .buffers = &cbuf, .count = 1 };

    ret = spi_write_dt(&cfg->bus, &ctx);
    if (ret < 0) {
        LOG_ERR("SPI cmd 0x%02X failed: %d", cmd, ret);
        return ret;
    }

    if (data && len > 0) {
        ret = gpio_pin_set_dt(&cfg->dc, 1);
        if (ret < 0) {
            return ret;
        }
        struct spi_buf dbuf = { .buf = (void *)data, .len = len };
        struct spi_buf_set dtx = { .buffers = &dbuf, .count = 1 };
        ret = spi_write_dt(&cfg->bus, &dtx);
        if (ret < 0) {
            LOG_ERR("SPI data (cmd 0x%02X, %u B) failed: %d", cmd,
                    (unsigned)len, ret);
            return ret;
        }
    }

    return 0;
}

static inline int _tx_u8(const struct ssd1683_config *cfg, uint8_t cmd,
                         uint8_t d0)
{
    return _tx(cfg, cmd, &d0, 1);
}

static int _hw_reset(const struct ssd1683_config *cfg)
{
    int ret;

    ret = gpio_pin_set_dt(&cfg->rst, 0);
    if (ret < 0) {
        return ret;
    }
    k_msleep(20);

    /* HIGH-LOW-HIGH pulse pattern required by many GDEY/SSD1683 modules to
     * reliably trigger the on-chip power-on-reset block. */
    gpio_pin_set_dt(&cfg->rst, 1);
    k_msleep(5);
    gpio_pin_set_dt(&cfg->rst, 0);
    k_msleep(10);
    gpio_pin_set_dt(&cfg->rst, 1);
    k_msleep(20);

    return _wait_busy(cfg, K_SECONDS(5), "hw_reset");
}

/* ------------------------------------------------------------------------ */
/* Section 2: init script and RAM addressing                                */
/* ------------------------------------------------------------------------ */

struct ssd1683_init_step {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[4];
};

/* Fixed portion of the init sequence (values independent of panel size). */
static const struct ssd1683_init_step k_init_fixed[] = {
    /* Soft start control - limits inrush current on low-power MCUs. */
    { SSD1683_CMD_SOFT_START,             4, { 0x8B, 0x9C, 0x96, 0x0F } },
    /* Gate driving voltage VGH = 20V (firm pixel lock). */
    { SSD1683_CMD_GATE_DRIVING_VOLTAGE,   1, { 0x17 } },
    /* Source driving: VSH1=15V, VSH2=5V, VSL=-15V. */
    { SSD1683_CMD_SOURCE_DRIVING_VOLTAGE, 3, { 0x41, 0x00, 0x32 } },
    /* VCOM = -1.0V (sweet spot for removing ghosting/grayness). */
    { SSD1683_CMD_WRITE_VCOM,             1, { 0x36 } },
    /* Border waveform: use LUT1 (white border for white background). */
    { SSD1683_CMD_BORDER_WAVEFORM,        1, { 0x01 } },
    /* Use built-in temperature sensor. */
    { SSD1683_CMD_TEMP_SENSOR,            1, { 0x80 } },
};

static int _set_ram_area(const struct ssd1683_config *cfg,
                         uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    int ret;

    /* X increment, Y increment. */
    ret = _tx_u8(cfg, SSD1683_CMD_DATA_ENTRY_MODE, 0x03);
    if (ret < 0) return ret;

    uint8_t xrange[2] = {
        (uint8_t)(x / 8),
        (uint8_t)((x + w - 1) / 8),
    };
    ret = _tx(cfg, SSD1683_CMD_SET_RAM_X, xrange, sizeof(xrange));
    if (ret < 0) return ret;

    uint8_t yrange[4] = {
        (uint8_t)(y & 0xFF),            (uint8_t)((y >> 8) & 0xFF),
        (uint8_t)((y + h - 1) & 0xFF),  (uint8_t)(((y + h - 1) >> 8) & 0xFF),
    };
    ret = _tx(cfg, SSD1683_CMD_SET_RAM_Y, yrange, sizeof(yrange));
    if (ret < 0) return ret;

    ret = _tx_u8(cfg, SSD1683_CMD_SET_RAM_X_COUNTER, (uint8_t)(x / 8));
    if (ret < 0) return ret;

    uint8_t yc[2] = { (uint8_t)(y & 0xFF), (uint8_t)((y >> 8) & 0xFF) };
    return _tx(cfg, SSD1683_CMD_SET_RAM_Y_COUNTER, yc, sizeof(yc));
}

static int _run_init_script(const struct device *dev)
{
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;
    int ret;

    if (data->is_initialized) {
        return 0;
    }

    ret = _hw_reset(cfg);
    if (ret < 0) return ret;
    k_msleep(10);

    ret = _tx(cfg, SSD1683_CMD_SWRESET, NULL, 0);
    if (ret < 0) return ret;
    ret = _wait_busy(cfg, K_SECONDS(2), "swreset");
    if (ret < 0) return ret;
    k_msleep(10);

    for (size_t i = 0; i < ARRAY_SIZE(k_init_fixed); i++) {
        const struct ssd1683_init_step *s = &k_init_fixed[i];
        ret = _tx(cfg, s->cmd, s->data, s->len);
        if (ret < 0) return ret;
    }

    /* Driver output control (MUX) depends on panel height.  Writes
     * (height-1) as 2 bytes + gate scan direction 0x00. */
    uint16_t mux = cfg->height - 1;
    uint8_t mux_data[3] = {
        (uint8_t)(mux & 0xFF),
        (uint8_t)((mux >> 8) & 0xFF),
        0x00,
    };
    ret = _tx(cfg, SSD1683_CMD_DRIVER_OUTPUT_CTRL, mux_data, sizeof(mux_data));
    if (ret < 0) return ret;

    ret = _set_ram_area(cfg, 0, 0, cfg->width, cfg->height);
    if (ret < 0) return ret;

    data->is_initialized = true;
    LOG_INF("hardware initialised");
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Section 3: push shadow to RAM, refresh sequences                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Push a byte-aligned rectangle from the shadow framebuffer into the
 *        given RAM bank (0x24 CURRENT or 0x26 PREVIOUS).
 *
 * The rect is expected to already be byte aligned on X and clipped to the
 * panel.  Internally writes one SPI transfer per row (max 50 bytes for a
 * 400-px-wide panel), yielding ~H transfers for a full screen instead of
 * W*H/8 (15 000) transfers like the old per-byte implementation.
 */
static int _push_rect(const struct ssd1683_config *cfg, uint8_t ram_cmd,
                      const struct ssd1683_dirty_rect *r)
{
    int ret;
    const uint16_t fb_stride = cfg->width / 8;
    const uint16_t row_bytes = r->w / 8;

    ret = _set_ram_area(cfg, r->x, r->y, r->w, r->h);
    if (ret < 0) return ret;

    /* Send the RAM write command once; then stream rows in data mode. */
    ret = gpio_pin_set_dt(&cfg->dc, 0);
    if (ret < 0) return ret;
    struct spi_buf cbuf = { .buf = &ram_cmd, .len = 1 };
    struct spi_buf_set ctx = { .buffers = &cbuf, .count = 1 };
    ret = spi_write_dt(&cfg->bus, &ctx);
    if (ret < 0) return ret;

    ret = gpio_pin_set_dt(&cfg->dc, 1);
    if (ret < 0) return ret;

    for (uint16_t row = 0; row < r->h; row++) {
        const uint8_t *line = cfg->shadow_fb +
                              (r->y + row) * fb_stride + (r->x / 8);
        struct spi_buf dbuf = { .buf = (void *)line, .len = row_bytes };
        struct spi_buf_set dtx = { .buffers = &dbuf, .count = 1 };
        ret = spi_write_dt(&cfg->bus, &dtx);
        if (ret < 0) {
            LOG_ERR("SPI row write failed: %d", ret);
            return ret;
        }
    }

    return 0;
}

static int _update_full(const struct device *dev)
{
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;
    int ret;

    /* Bypass RED (monochrome) + single chip. */
    uint8_t upd_ctrl[2] = { 0x40, 0x00 };
    ret = _tx(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL, upd_ctrl, 2);
    if (ret < 0) return ret;

    if (data->use_fast_update) {
        ret = _tx_u8(cfg, SSD1683_CMD_WRITE_TEMP_REG, 0x6E);
        if (ret < 0) return ret;
        ret = _tx_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
                     SSD1683_UDC2_FULL_FAST);
    } else {
        ret = _tx_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
                     SSD1683_UDC2_FULL_SLOW);
    }
    if (ret < 0) return ret;

    ret = _tx(cfg, SSD1683_CMD_MASTER_ACTIVATION, NULL, 0);
    if (ret < 0) return ret;

    ret = _wait_busy(cfg, K_SECONDS(5), "full_update");
    if (ret < 0) return ret;

    data->is_powered_on = false; /* full update sequence ends with power off */
    return 0;
}

static int _update_partial(const struct device *dev)
{
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;
    int ret;

    uint8_t upd_ctrl[2] = { 0x00, 0x00 };
    ret = _tx(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL, upd_ctrl, 2);
    if (ret < 0) return ret;

    ret = _tx_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
                 SSD1683_UDC2_PARTIAL);
    if (ret < 0) return ret;

    ret = _tx(cfg, SSD1683_CMD_MASTER_ACTIVATION, NULL, 0);
    if (ret < 0) return ret;

    /* Partial refreshes on a GDEY042T81 at room temperature finish in
     * ~400-600ms, but can briefly spike past 2s after a long burst of
     * partials (VCOM drift, temp sensor reload, noise recovery).  We
     * give the same 5s headroom as full updates so only a truly stuck
     * BUSY trips the timeout, and the work handler can then recover. */
    ret = _wait_busy(cfg, K_SECONDS(5), "partial_update");
    if (ret < 0) return ret;

    data->is_powered_on = true; /* partial leaves the analog rail energised */
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Section 4: workqueue handler                                             */
/* ------------------------------------------------------------------------ */

static inline bool _rect_empty(const struct ssd1683_dirty_rect *r)
{
    return r->w == 0 || r->h == 0;
}

static inline bool _rect_is_full(const struct ssd1683_dirty_rect *r,
                                 const struct ssd1683_config *cfg)
{
    return r->x == 0 && r->y == 0 &&
           r->w == cfg->width && r->h == cfg->height;
}

/**
 * @brief Execute one complete refresh sequence (init-if-needed, push RAM,
 *        trigger update, mirror to RAM-B).
 *
 * Factored out of @ref _refresh_work_handler so the handler can invoke it
 * twice: once normally and, on `-ETIMEDOUT`, once more after a hardware
 * reset.  Returns 0 on success or a negative errno.
 */
static int _do_refresh_once(const struct device *dev, bool do_full,
                            const struct ssd1683_dirty_rect *rect)
{
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;
    int ret;

    if (!data->is_initialized) {
        ret = _run_init_script(dev);
        if (ret < 0) {
            LOG_ERR("init_script: %d", ret);
            return ret;
        }
    }

    if (do_full) {
        struct ssd1683_dirty_rect full_rect = {
            .x = 0, .y = 0, .w = cfg->width, .h = cfg->height,
        };
        ret = _push_rect(cfg, SSD1683_CMD_WRITE_RAM_CURRENT, &full_rect);
        if (ret < 0) return ret;
        ret = _update_full(dev);
        if (ret < 0) return ret;
        /* Sync RAM-B with what was just displayed so the NEXT partial has
         * a correct "previous" reference. */
        return _push_rect(cfg, SSD1683_CMD_WRITE_RAM_PREVIOUS, &full_rect);
    }

    ret = _push_rect(cfg, SSD1683_CMD_WRITE_RAM_CURRENT, rect);
    if (ret < 0) return ret;
    ret = _update_partial(dev);
    if (ret < 0) return ret;
    /* Sync RAM-B with new data for the updated rect only. */
    return _push_rect(cfg, SSD1683_CMD_WRITE_RAM_PREVIOUS, rect);
}

static void _refresh_work_handler(struct k_work *w)
{
    struct k_work_delayable *dw = k_work_delayable_from_work(w);
    struct ssd1683_data *data =
        CONTAINER_OF(dw, struct ssd1683_data, refresh_work);
    const struct device *dev = data->self;
    const struct ssd1683_config *cfg = dev->config;
    int ret;
    int64_t t0 = k_uptime_get();

    struct ssd1683_dirty_rect rect;
    bool do_full;

    k_mutex_lock(&data->lock, K_FOREVER);
    rect = data->dirty;
    data->dirty = (struct ssd1683_dirty_rect){ 0 };
    do_full = data->force_full ||
              data->partial_count >= CONFIG_SSD1683_PARTIAL_MAX_BEFORE_FULL ||
              _rect_is_full(&rect, cfg);
    k_mutex_unlock(&data->lock);

    if (_rect_empty(&rect)) {
        k_sem_give(&data->refresh_done);
        return;
    }

    ret = _do_refresh_once(dev, do_full, &rect);

    /* Recovery: a stuck-BUSY is a known SSD168x failure mode (brief
     * electrical glitch, VCC droop, charge-pump lockup after a burst of
     * partials).  The only reliable way out is a hardware reset + full
     * re-init.  We also escalate the retry to a FULL refresh because it
     * re-seeds RAM-B and clears any residual waveform state that might
     * have helped put the panel into the bad state in the first place. */
    if (ret == -ETIMEDOUT) {
        LOG_WRN("recovering from BUSY timeout: hw reset + full refresh");
        data->is_initialized = false;
        data->is_powered_on  = false;
        do_full = true;
        ret = _do_refresh_once(dev, true, &rect);
        if (ret == 0) {
            LOG_INF("recovery succeeded");
        }
    }

    if (ret < 0) {
        LOG_ERR("refresh failed: %d", ret);
        /* Leave the panel marked uninitialised so the NEXT refresh starts
         * from a clean hw-reset instead of re-entering the bad state. */
        data->is_initialized = false;
        data->is_powered_on  = false;
    } else if (do_full) {
        data->partial_count = 0;
        data->force_full    = false;
        LOG_INF("full refresh: %lld ms", k_uptime_get() - t0);
    } else {
        data->partial_count++;
        LOG_INF("partial refresh %ux%u@%u,%u : %lld ms (count=%u)",
                rect.w, rect.h, rect.x, rect.y,
                k_uptime_get() - t0, data->partial_count);
    }

    k_sem_give(&data->refresh_done);
}

/* ------------------------------------------------------------------------ */
/* Helper - union dirty rect                                                */
/* ------------------------------------------------------------------------ */

/* Must be called with data->lock held. */
static void _dirty_union(struct ssd1683_dirty_rect *d,
                         uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    /* Byte-align on X to match how the SSD1683 addresses RAM. */
    uint16_t x0 = (x / 8) * 8;
    uint16_t x1 = ((x + w + 7) / 8) * 8;
    uint16_t y0 = y;
    uint16_t y1 = y + h;

    if (_rect_empty(d)) {
        d->x = x0; d->y = y0;
        d->w = x1 - x0; d->h = y1 - y0;
        return;
    }
    uint16_t dx0 = d->x;
    uint16_t dx1 = d->x + d->w;
    uint16_t dy0 = d->y;
    uint16_t dy1 = d->y + d->h;

    if (x0 < dx0) dx0 = x0;
    if (x1 > dx1) dx1 = x1;
    if (y0 < dy0) dy0 = y0;
    if (y1 > dy1) dy1 = y1;

    d->x = dx0; d->y = dy0;
    d->w = dx1 - dx0; d->h = dy1 - dy0;
}

/* Exposed to the display wrapper (internal linkage across TUs is not
 * convenient, so we just export it via the header-less static inline pattern
 * is impossible; instead we add a non-static function below). */
void _ssd1683_stage_rect(const struct device *dev, uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h, const uint8_t *src,
                         uint16_t src_pitch);

void _ssd1683_stage_rect(const struct device *dev, uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h, const uint8_t *src,
                         uint16_t src_pitch)
{
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);

    /* Copy incoming MSB-first mono bytes into the shadow.  The Zephyr
     * display shim for LVGL guarantees x is byte aligned when the panel
     * advertises SCREEN_INFO_MONO_MSB_FIRST.  We still defensively align. */
    uint16_t x_bytes = x / 8;
    uint16_t w_bytes = (w + 7) / 8;
    uint16_t fb_stride = cfg->width / 8;

    for (uint16_t row = 0; row < h; row++) {
        uint8_t *dst_row = cfg->shadow_fb + (y + row) * fb_stride + x_bytes;
        const uint8_t *src_row = src + row * src_pitch;
        memcpy(dst_row, src_row, w_bytes);
    }

    _dirty_union(&data->dirty, x, y, w, h);
    k_sem_reset(&data->refresh_done);
    k_mutex_unlock(&data->lock);

    k_work_reschedule_for_queue(cfg->workq, &data->refresh_work,
                                K_MSEC(CONFIG_SSD1683_FLUSH_DELAY_MS));
}

/* ------------------------------------------------------------------------ */
/* Section 5: public API                                                    */
/* ------------------------------------------------------------------------ */

int ssd1683_flush(const struct device *dev, k_timeout_t timeout)
{
    if (!dev) return -EINVAL;
    struct ssd1683_data *data = dev->data;
    const struct ssd1683_config *cfg = dev->config;

    /* Check if there's anything staged.  If so, kick the work now (bypass
     * the coalescing delay). */
    k_mutex_lock(&data->lock, K_FOREVER);
    bool has_work = !_rect_empty(&data->dirty);
    k_mutex_unlock(&data->lock);

    if (has_work) {
        k_work_reschedule_for_queue(cfg->workq, &data->refresh_work, K_NO_WAIT);
    } else {
        return 0;
    }

    int ret = k_sem_take(&data->refresh_done, timeout);
    if (ret == -EAGAIN) {
        LOG_WRN("ssd1683_flush: timeout");
    }
    return ret;
}

bool ssd1683_is_busy(const struct device *dev)
{
    if (!dev) return false;
    struct ssd1683_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    bool staged = !_rect_empty(&data->dirty);
    k_mutex_unlock(&data->lock);

    if (staged) return true;
    return k_work_delayable_busy_get(&data->refresh_work) != 0;
}

int ssd1683_force_full_refresh(const struct device *dev)
{
    if (!dev) return -EINVAL;
    struct ssd1683_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    data->force_full = true;
    k_mutex_unlock(&data->lock);
    return 0;
}

int ssd1683_clear_screen(const struct device *dev, uint8_t value)
{
    if (!dev) return -EINVAL;
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    memset(cfg->shadow_fb, value, cfg->shadow_fb_size);
    _dirty_union(&data->dirty, 0, 0, cfg->width, cfg->height);
    data->force_full = true;
    k_sem_reset(&data->refresh_done);
    k_mutex_unlock(&data->lock);

    k_work_reschedule_for_queue(cfg->workq, &data->refresh_work, K_NO_WAIT);
    return k_sem_take(&data->refresh_done, K_SECONDS(6));
}

int ssd1683_power_on(const struct device *dev)
{
    if (!dev) return -EINVAL;
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;
    int ret;

    if (data->is_powered_on) return 0;

    if (!data->is_initialized) {
        ret = _run_init_script(dev);
        if (ret < 0) return ret;
    }

    ret = _tx_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
                 SSD1683_UDC2_POWER_ON);
    if (ret < 0) return ret;
    ret = _tx(cfg, SSD1683_CMD_MASTER_ACTIVATION, NULL, 0);
    if (ret < 0) return ret;
    ret = _wait_busy(cfg, K_SECONDS(2), "power_on");
    if (ret < 0) {
        LOG_WRN("power_on quick-resume failed, forcing re-init");
        data->is_initialized = false;
        return _run_init_script(dev);
    }

    data->is_powered_on = true;
    return 0;
}

int ssd1683_power_off(const struct device *dev)
{
    if (!dev) return -EINVAL;
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;
    int ret;

    if (!data->is_powered_on) return 0;

    ret = _tx_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
                 SSD1683_UDC2_POWER_OFF_ANALOG);
    if (ret < 0) return ret;
    ret = _tx(cfg, SSD1683_CMD_MASTER_ACTIVATION, NULL, 0);
    if (ret < 0) return ret;
    ret = _wait_busy(cfg, K_SECONDS(2), "power_off");
    if (ret < 0) return ret;

    data->is_powered_on = false;
    return 0;
}

int ssd1683_hibernate(const struct device *dev)
{
    if (!dev) return -EINVAL;
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;
    int ret;

    ret = ssd1683_power_off(dev);
    if (ret < 0) return ret;

    ret = _tx_u8(cfg, SSD1683_CMD_DEEP_SLEEP, 0x01);
    if (ret < 0) return ret;

    data->is_initialized = false;
    return 0;
}

int ssd1683_set_fast_update(const struct device *dev, bool fast_update)
{
    if (!dev) return -EINVAL;
    struct ssd1683_data *data = dev->data;
    data->use_fast_update = fast_update;
    return 0;
}

bool ssd1683_is_powered_on(const struct device *dev)
{
    if (!dev) return false;
    struct ssd1683_data *data = dev->data;
    return data->is_powered_on;
}

bool ssd1683_is_initialized(const struct device *dev)
{
    if (!dev) return false;
    struct ssd1683_data *data = dev->data;
    return data->is_initialized;
}

/* ------------------------------------------------------------------------ */
/* Driver bring-up (called from the display wrapper's init)                 */
/* ------------------------------------------------------------------------ */

int _ssd1683_bringup(const struct device *dev);
int _ssd1683_bringup(const struct device *dev)
{
    const struct ssd1683_config *cfg = dev->config;
    struct ssd1683_data *data = dev->data;
    int ret;

    if (!spi_is_ready_dt(&cfg->bus))  { LOG_ERR("SPI not ready");  return -ENODEV; }
    if (!gpio_is_ready_dt(&cfg->dc))  { LOG_ERR("DC not ready");   return -ENODEV; }
    if (!gpio_is_ready_dt(&cfg->rst)) { LOG_ERR("RST not ready");  return -ENODEV; }
    if (!gpio_is_ready_dt(&cfg->busy)){ LOG_ERR("BUSY not ready"); return -ENODEV; }

    ret = gpio_pin_configure_dt(&cfg->dc,  GPIO_OUTPUT_ACTIVE);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&cfg->rst, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&cfg->busy, GPIO_INPUT);
    if (ret < 0) return ret;

    data->self = dev;
    data->is_initialized = false;
    data->is_powered_on  = false;
    data->is_blanked     = false;
    data->use_fast_update = cfg->fast_mode;
    data->force_full     = false;
    data->partial_count  = 0;
    data->dirty = (struct ssd1683_dirty_rect){ 0 };

    k_mutex_init(&data->lock);
    k_sem_init(&data->refresh_done, 0, 1);
    k_work_init_delayable(&data->refresh_work, _refresh_work_handler);

    static const struct k_work_queue_config wq_cfg = {
        .name = "ssd1683_wq",
    };
    k_work_queue_init(cfg->workq);
    k_work_queue_start(cfg->workq, cfg->workq_stack, cfg->workq_stack_size,
                       CONFIG_SSD1683_WORKQUEUE_PRIORITY, &wq_cfg);

    /* Initialise the shadow framebuffer to white. Hardware init is
     * deferred: it runs lazily on the first power_on/refresh, after the
     * application has enabled the panel's power rails. Doing SPI traffic
     * here at POST_KERNEL would talk to an unpowered panel - BUSY would
     * either be floating (hang the 5 s _wait_busy) or read low and let us
     * "succeed" silently while having configured nothing.
     */
    memset(cfg->shadow_fb, 0xFF, cfg->shadow_fb_size);

    LOG_INF("registered: %ux%u, fast=%d (hw init deferred)",
            cfg->width, cfg->height, (int)data->use_fast_update);
    return 0;
}
