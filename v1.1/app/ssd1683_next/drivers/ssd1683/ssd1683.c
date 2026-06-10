/**
 * @file ssd1683.c
 * @brief SSD1683 E-Paper Display Driver - low level
 *
 * Static helpers and public API. Device instantiation lives in
 * ssd1683_display.c.
 */

#include "ssd1683.h"

#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ssd1683, CONFIG_SSD1683_LOG_LEVEL);

static int _ssd1683_ensure_initialized(const struct device *dev);

static int _ssd1683_bringup_nolock(const struct device *dev, bool preserve_shadow);
static int _ssd1683_cold_start_recovery_nolock(const struct device *dev);
static int _do_refresh_once_nolock(const struct device *dev, bool do_full,
				   const struct ssd1683_dirty_rect *rect);

int _ssd1683_bringup(const struct device *dev, bool preserve_shadow);

/* ------------------------------------------------------------------------ */
/* Section 1: low-level SPI / GPIO                                          */
/* ------------------------------------------------------------------------ */

static int _ssd1683_wait_busy(const struct ssd1683_config *cfg,
			      k_timeout_t timeout, const char *phase)
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

static int _ssd1683_write_cmd(const struct ssd1683_config *cfg, uint8_t cmd)
{
	int ret;

	ret = gpio_pin_set_dt(&cfg->dc, 0);
	if (ret < 0) {
		return ret;
	}

	struct spi_buf buf = { .buf = &cmd, .len = 1 };
	struct spi_buf_set tx = { .buffers = &buf, .count = 1 };

	ret = spi_write_dt(&cfg->bus, &tx);
	if (ret < 0) {
		LOG_ERR("SPI cmd 0x%02X failed: %d", cmd, ret);
	}
	return ret;
}

static int _ssd1683_write_data(const struct ssd1683_config *cfg,
			       const void *data, size_t len)
{
	int ret;

	if (len == 0) {
		return 0;
	}

	ret = gpio_pin_set_dt(&cfg->dc, 1);
	if (ret < 0) {
		return ret;
	}

	struct spi_buf buf = { .buf = (void *)data, .len = len };
	struct spi_buf_set tx = { .buffers = &buf, .count = 1 };

	ret = spi_write_dt(&cfg->bus, &tx);
	if (ret < 0) {
		LOG_ERR("SPI data write failed: %d", ret);
	}
	return ret;
}

static inline int _ssd1683_write_u8(const struct ssd1683_config *cfg,
				    uint8_t cmd, uint8_t d0)
{
	int ret;

	ret = _ssd1683_write_cmd(cfg, cmd);
	if (ret < 0) {
		return ret;
	}
	return _ssd1683_write_data(cfg, &d0, 1);
}

static int _ssd1683_write_ram_burst(const struct ssd1683_config *cfg,
				    uint8_t cmd, const uint8_t *data,
				    size_t len)
{
	int ret;

	ret = _ssd1683_write_cmd(cfg, cmd);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_set_dt(&cfg->dc, 1);
	if (ret < 0) {
		return ret;
	}
	if (len == 0) {
		return 0;
	}

	struct spi_buf buf = { .buf = (void *)data, .len = len };
	struct spi_buf_set tx = { .buffers = &buf, .count = 1 };

	ret = spi_write_dt(&cfg->bus, &tx);
	if (ret < 0) {
		LOG_ERR("SPI RAM burst (cmd 0x%02X, %u B) failed: %d", cmd,
			(unsigned)len, ret);
	}
	return ret;
}

static int _ssd1683_reset(const struct ssd1683_config *cfg)
{
	int ret;

	ret = gpio_pin_set_dt(&cfg->rst, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(20);

	ret = gpio_pin_set_dt(&cfg->rst, 1);
	if (ret < 0) {
		return ret;
	}
	k_msleep(5);
	ret = gpio_pin_set_dt(&cfg->rst, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(10);
	ret = gpio_pin_set_dt(&cfg->rst, 1);
	if (ret < 0) {
		return ret;
	}
	k_msleep(20);

	return _ssd1683_wait_busy(cfg, K_SECONDS(5), "hw_reset");
}

/* ------------------------------------------------------------------------ */
/* Section 2: init script and RAM addressing                                */
/* ------------------------------------------------------------------------ */

struct ssd1683_init_step {
	uint8_t cmd;
	uint8_t len;
	uint8_t data[4];
};

static const struct ssd1683_init_step k_init_fixed[] = {
	{ SSD1683_CMD_SOFT_START, 4, { 0x8B, 0x9C, 0x96, 0x0F } },
	{ SSD1683_CMD_GATE_DRIVING_VOLTAGE, 1, { 0x17 } },
	{ SSD1683_CMD_SOURCE_DRIVING_VOLTAGE, 3, { 0x41, 0x00, 0x32 } },
	{ SSD1683_CMD_WRITE_VCOM, 1, { 0x36 } },
	{ SSD1683_CMD_BORDER_WAVEFORM, 1, { 0x01 } },
	{ SSD1683_CMD_TEMP_SENSOR, 1, { 0x80 } },
};

static int _ssd1683_set_partial_ram_area(const struct ssd1683_config *cfg,
					 uint16_t x, uint16_t y, uint16_t w,
					 uint16_t h)
{
	int ret;
	uint8_t xrange[2];
	uint8_t yrange[4];
	uint8_t yc[2];

	ret = _ssd1683_write_u8(cfg, SSD1683_CMD_DATA_ENTRY_MODE, 0x03);
	if (ret < 0) {
		return ret;
	}

	xrange[0] = (uint8_t)(x / 8);
	xrange[1] = (uint8_t)((x + w - 1) / 8);
	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X);
	if (ret < 0) {
		return ret;
	}
	ret = _ssd1683_write_data(cfg, xrange, sizeof(xrange));
	if (ret < 0) {
		return ret;
	}

	yrange[0] = (uint8_t)(y & 0xFF);
	yrange[1] = (uint8_t)((y >> 8) & 0xFF);
	yrange[2] = (uint8_t)((y + h - 1) & 0xFF);
	yrange[3] = (uint8_t)(((y + h - 1) >> 8) & 0xFF);
	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y);
	if (ret < 0) {
		return ret;
	}
	ret = _ssd1683_write_data(cfg, yrange, sizeof(yrange));
	if (ret < 0) {
		return ret;
	}

	ret = _ssd1683_write_u8(cfg, SSD1683_CMD_SET_RAM_X_COUNTER,
				(uint8_t)(x / 8));
	if (ret < 0) {
		return ret;
	}

	yc[0] = (uint8_t)(y & 0xFF);
	yc[1] = (uint8_t)((y >> 8) & 0xFF);
	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y_COUNTER);
	if (ret < 0) {
		return ret;
	}
	return _ssd1683_write_data(cfg, yc, sizeof(yc));
}

static int _ssd1683_gpio_configure(const struct device *dev)
{
	const struct ssd1683_config *cfg = dev->config;
	int ret;

	if (!spi_is_ready_dt(&cfg->bus)) {
		LOG_ERR("SPI not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&cfg->dc)) {
		LOG_ERR("DC not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&cfg->rst)) {
		LOG_ERR("RST not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&cfg->busy)) {
		LOG_ERR("BUSY not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->dc, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->rst, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->busy, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int _ssd1683_cold_start_recovery(const struct device *dev)
{
	struct ssd1683_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = _ssd1683_cold_start_recovery_nolock(dev);
	k_mutex_unlock(&data->lock);
	return ret;
}

static int _ssd1683_cold_start_recovery_nolock(const struct device *dev)
{
	struct ssd1683_data *data = dev->data;
	int ret;

	LOG_WRN("cold-start recovery");
	data->is_initialized = false;
	data->is_powered_on = false;
	data->needs_full_sync = true;

	ret = _ssd1683_bringup_nolock(dev, true);
	if (ret < 0) {
		data->is_initialized = false;
		data->is_powered_on = false;
		return ret;
	}

	data->is_powered_on = true;
	return 0;
}

static int _ssd1683_ensure_initialized(const struct device *dev)
{
	struct ssd1683_data *data = dev->data;

	if (data->is_initialized) {
		return 0;
	}
	return _ssd1683_bringup(dev, true);
}

/* ------------------------------------------------------------------------ */
/* Section 3: push shadow to RAM, refresh sequences                         */
/* ------------------------------------------------------------------------ */

static int _ssd1683_push_region(const struct ssd1683_config *cfg,
				uint8_t ram_cmd,
				const struct ssd1683_dirty_rect *r)
{
	const uint16_t fb_stride = cfg->width / 8;
	const uint16_t row_bytes = r->w / 8;
	const size_t payload_len = (size_t)row_bytes * r->h;
	int ret;

	ret = _ssd1683_set_partial_ram_area(cfg, r->x, r->y, r->w, r->h);
	if (ret < 0) {
		return ret;
	}

	/* Full-width band: one contiguous slice in shadow FB. */
	if (r->w == cfg->width) {
		const uint8_t *src = cfg->shadow_fb + (size_t)r->y * fb_stride;

		return _ssd1683_write_ram_burst(cfg, ram_cmd, src, payload_len);
	}

	/* Narrow region: one RAM cmd, then one SPI row per scanline (no
	 * on-stack staging — a 1536 B buffer overflowed the 2048 B WQ). */
	ret = _ssd1683_write_cmd(cfg, ram_cmd);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_set_dt(&cfg->dc, 1);
	if (ret < 0) {
		return ret;
	}

	for (uint16_t row = 0; row < r->h; row++) {
		const uint8_t *line = cfg->shadow_fb +
				      (size_t)(r->y + row) * fb_stride +
				      (r->x / 8);
		struct spi_buf buf = { .buf = (void *)line, .len = row_bytes };
		struct spi_buf_set tx = { .buffers = &buf, .count = 1 };

		ret = spi_write_dt(&cfg->bus, &tx);
		if (ret < 0) {
			LOG_ERR("SPI row write failed: %d", ret);
			return ret;
		}
	}

	return 0;
}

static int _ssd1683_push_full(const struct ssd1683_config *cfg,
			      uint8_t ram_cmd)
{
	int ret;

	ret = _ssd1683_set_partial_ram_area(cfg, 0, 0, cfg->width, cfg->height);
	if (ret < 0) {
		return ret;
	}
	return _ssd1683_write_ram_burst(cfg, ram_cmd, cfg->shadow_fb,
					cfg->shadow_fb_size);
}

static int _ssd1683_update_full(const struct device *dev)
{
	const struct ssd1683_config *cfg = dev->config;
	struct ssd1683_data *data = dev->data;
	int ret;
	uint8_t upd_ctrl[2] = { 0x40, 0x00 };

	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL);
	if (ret < 0) {
		return ret;
	}
	ret = _ssd1683_write_data(cfg, upd_ctrl, sizeof(upd_ctrl));
	if (ret < 0) {
		return ret;
	}

	if (data->use_fast_update) {
		ret = _ssd1683_write_u8(cfg, SSD1683_CMD_WRITE_TEMP_REG, 0x6E);
		if (ret < 0) {
			return ret;
		}
		ret = _ssd1683_write_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
					SSD1683_UDC2_FULL_FAST);
	} else {
		ret = _ssd1683_write_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
					SSD1683_UDC2_FULL_SLOW);
	}
	if (ret < 0) {
		return ret;
	}

	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
	if (ret < 0) {
		return ret;
	}

	ret = _ssd1683_wait_busy(cfg, K_SECONDS(5), "full_update");
	if (ret < 0) {
		return ret;
	}

	data->is_powered_on = false;
	return 0;
}

static int _ssd1683_update_partial(const struct device *dev)
{
	const struct ssd1683_config *cfg = dev->config;
	struct ssd1683_data *data = dev->data;
	int ret;
	uint8_t upd_ctrl[2] = { 0x00, 0x00 };

	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL);
	if (ret < 0) {
		return ret;
	}
	ret = _ssd1683_write_data(cfg, upd_ctrl, sizeof(upd_ctrl));
	if (ret < 0) {
		return ret;
	}

	ret = _ssd1683_write_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
				SSD1683_UDC2_PARTIAL);
	if (ret < 0) {
		return ret;
	}

	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
	if (ret < 0) {
		return ret;
	}

	ret = _ssd1683_wait_busy(cfg, K_SECONDS(5), "partial_update");
	if (ret < 0) {
		return ret;
	}

	data->is_powered_on = true;
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
	return r->x == 0 && r->y == 0 && r->w == cfg->width &&
	       r->h == cfg->height;
}

static bool _should_full_refresh(const struct ssd1683_data *data,
				 const struct ssd1683_config *cfg,
				 const struct ssd1683_dirty_rect *rect,
				 const char **reason_out)
{
	if (data->force_full) {
		if (reason_out) {
			*reason_out = "force_full";
		}
		return true;
	}
	if (data->partial_count >= CONFIG_SSD1683_PARTIAL_LIMIT) {
		if (reason_out) {
			*reason_out = "partial_limit";
		}
		return true;
	}
	if (_rect_is_full(rect, cfg)) {
		if (reason_out) {
			*reason_out = "rect_full";
		}
		return true;
	}
	if (reason_out) {
		*reason_out = NULL;
	}
	return false;
}

static int _do_refresh_once_nolock(const struct device *dev, bool do_full,
				  const struct ssd1683_dirty_rect *rect)
{
	const struct ssd1683_config *cfg = dev->config;
	struct ssd1683_data *data = dev->data;
	int ret;

	if (!data->is_initialized) {
		ret = _ssd1683_bringup_nolock(dev, true);
		if (ret < 0) {
			LOG_ERR("bringup in refresh: %d", ret);
			return ret;
		}
	}

	if (do_full) {
		ret = _ssd1683_push_full(cfg, SSD1683_CMD_WRITE_RAM_CURRENT);
		if (ret < 0) {
			return ret;
		}
		ret = _ssd1683_update_full(dev);
		if (ret < 0) {
			return ret;
		}
		/* Sync RAM-B after display so the next partial has a correct
		 * "previous" reference (matches v1.1 ordering). */
		return _ssd1683_push_full(cfg, SSD1683_CMD_WRITE_RAM_PREVIOUS);
	}

	/* Partial: CURRENT=new, chip PREVIOUS=last frame; update; then sync
	 * PREVIOUS. Pushing PREVIOUS before update wrote identical bytes to
	 * both banks and caused faint ghost accumulation. */
	ret = _ssd1683_push_region(cfg, SSD1683_CMD_WRITE_RAM_CURRENT, rect);
	if (ret < 0) {
		return ret;
	}
	ret = _ssd1683_update_partial(dev);
	if (ret < 0) {
		return ret;
	}
	return _ssd1683_push_region(cfg, SSD1683_CMD_WRITE_RAM_PREVIOUS, rect);
}

void _ssd1683_refresh_work(struct k_work *w)
{
	struct k_work_delayable *dw = k_work_delayable_from_work(w);
	struct ssd1683_data *data =
		CONTAINER_OF(dw, struct ssd1683_data, refresh_work);
	const struct device *dev = data->self;
	const struct ssd1683_config *cfg = dev->config;
	struct ssd1683_dirty_rect rect;
	bool do_full;
	const char *full_reason = NULL;
	int ret;
	int64_t t0 = k_uptime_get();

	k_mutex_lock(&data->lock, K_FOREVER);

	if (_rect_empty(&data->dirty)) {
		k_mutex_unlock(&data->lock);
		k_sem_give(&data->refresh_done);
		return;
	}

	rect = data->dirty;
	do_full = _should_full_refresh(data, cfg, &rect, &full_reason);
	data->dirty = (struct ssd1683_dirty_rect){ 0 };

	ret = _do_refresh_once_nolock(dev, do_full, &rect);

	if (ret == -ETIMEDOUT) {
		LOG_WRN("recovering from BUSY timeout: hw reset + full refresh");
		ret = _ssd1683_cold_start_recovery_nolock(dev);
		if (ret == 0) {
			do_full = true;
			full_reason = "recovery";
			ret = _do_refresh_once_nolock(dev, true, &rect);
			if (ret == 0) {
				LOG_INF("recovery succeeded");
			}
		}
	}

	if (ret < 0) {
		LOG_ERR("refresh failed: %d", ret);
		data->is_initialized = false;
		data->is_powered_on = false;
	} else {
		data->force_full = false;
		if (do_full) {
			data->needs_full_sync = false;
			data->partial_count = 0;
			LOG_INF("full refresh %ux%u@%u,%u (%s): %lld ms",
				rect.w, rect.h, rect.x, rect.y,
				full_reason ? full_reason : "recovery",
				k_uptime_get() - t0);
		} else {
			data->needs_full_sync = false;
			data->partial_count++;
			LOG_INF("partial refresh %ux%u@%u,%u : %lld ms (count=%u)",
				rect.w, rect.h, rect.x, rect.y,
				k_uptime_get() - t0, data->partial_count);
		}
	}

	k_mutex_unlock(&data->lock);
	k_sem_give(&data->refresh_done);
}

static void _dirty_union(struct ssd1683_dirty_rect *d, uint16_t x, uint16_t y,
			 uint16_t w, uint16_t h)
{
	uint16_t x0;
	uint16_t y0;
	uint16_t x1;
	uint16_t y1;

	if (w == 0 || h == 0) {
		return;
	}

	x &= ~7U;
	w = ((w + 7) / 8) * 8;

	if (_rect_empty(d)) {
		d->x = x;
		d->y = y;
		d->w = w;
		d->h = h;
		return;
	}

	x0 = MIN(d->x, x);
	y0 = MIN(d->y, y);
	x1 = MAX(d->x + d->w, x + w);
	y1 = MAX(d->y + d->h, y + h);

	d->x = x0;
	d->y = y0;
	d->w = x1 - x0;
	d->h = y1 - y0;
}

void _ssd1683_stage_rect(const struct device *dev, uint16_t x, uint16_t y,
			 uint16_t w, uint16_t h, const uint8_t *src,
			 uint16_t src_pitch)
{
	const struct ssd1683_config *cfg = dev->config;
	struct ssd1683_data *data = dev->data;
	const uint16_t x_bytes = x / 8;
	const uint16_t w_bytes = (w + 7) / 8;
	const uint16_t fb_stride = cfg->width / 8;
	int ret;

	/* Shadow FB: cfg->width/8 bytes per row, MSB = leftmost pixel,
	 * 1=white / 0=black (MONO01). No invert — matches LVGL I1 palette. */

	ret = _ssd1683_ensure_initialized(dev);
	if (ret < 0) {
		LOG_ERR("stage_rect: ensure_initialized: %d", ret);
		return;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

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
	struct ssd1683_data *data;
	const struct ssd1683_config *cfg;
	bool has_staged;
	bool work_pending;
	int ret;

	if (!dev) {
		return -EINVAL;
	}

	data = dev->data;
	cfg = dev->config;

	k_mutex_lock(&data->lock, K_FOREVER);
	has_staged = !_rect_empty(&data->dirty);
	k_mutex_unlock(&data->lock);

	work_pending = k_work_delayable_busy_get(&data->refresh_work) != 0;

	if (!has_staged && !work_pending) {
		return 0;
	}

	k_work_reschedule_for_queue(cfg->workq, &data->refresh_work, K_NO_WAIT);

	ret = k_sem_take(&data->refresh_done, timeout);
	if (ret == -EAGAIN) {
		LOG_WRN("ssd1683_flush: timeout");
	}
	return ret;
}

bool ssd1683_is_busy(const struct device *dev)
{
	struct ssd1683_data *data;
	bool staged;

	if (!dev) {
		return false;
	}

	data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	staged = !_rect_empty(&data->dirty);
	k_mutex_unlock(&data->lock);

	if (staged) {
		return true;
	}
	return k_work_delayable_busy_get(&data->refresh_work) != 0;
}

int ssd1683_force_full_refresh(const struct device *dev)
{
	struct ssd1683_data *data;

	if (!dev) {
		return -EINVAL;
	}

	data = dev->data;
	k_mutex_lock(&data->lock, K_FOREVER);
	data->force_full = true;
	k_mutex_unlock(&data->lock);
	return 0;
}

int ssd1683_clear_screen(const struct device *dev, uint8_t value)
{
	const struct ssd1683_config *cfg;
	struct ssd1683_data *data;

	if (!dev) {
		return -EINVAL;
	}

	cfg = dev->config;
	data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	memset(cfg->shadow_fb, value, cfg->shadow_fb_size);
	_dirty_union(&data->dirty, 0, 0, cfg->width, cfg->height);
	data->needs_full_sync = true;
	k_sem_reset(&data->refresh_done);
	k_mutex_unlock(&data->lock);

	k_work_reschedule_for_queue(cfg->workq, &data->refresh_work, K_NO_WAIT);
	return k_sem_take(&data->refresh_done, K_SECONDS(6));
}

int ssd1683_power_on(const struct device *dev)
{
	const struct ssd1683_config *cfg;
	struct ssd1683_data *data;
	int ret;

	if (!dev) {
		return -EINVAL;
	}

	cfg = dev->config;
	data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->is_powered_on) {
		k_mutex_unlock(&data->lock);
		return 0;
	}

	if (!data->is_initialized) {
		ret = _ssd1683_bringup_nolock(dev, true);
		if (ret < 0) {
			k_mutex_unlock(&data->lock);
			return ret;
		}
	}

	ret = _ssd1683_write_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
				SSD1683_UDC2_POWER_ON);
	if (ret < 0) {
		goto cold_start;
	}
	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
	if (ret < 0) {
		goto cold_start;
	}
	ret = _ssd1683_wait_busy(cfg, K_SECONDS(2), "power_on");
	if (ret < 0) {
		goto cold_start;
	}

	data->is_powered_on = true;
	k_mutex_unlock(&data->lock);
	return 0;

cold_start:
	ret = _ssd1683_cold_start_recovery_nolock(dev);
	k_mutex_unlock(&data->lock);
	return ret;
}

int ssd1683_power_off(const struct device *dev)
{
	const struct ssd1683_config *cfg;
	struct ssd1683_data *data;
	int ret;

	if (!dev) {
		return -EINVAL;
	}

	cfg = dev->config;
	data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (!data->is_powered_on) {
		k_mutex_unlock(&data->lock);
		return 0;
	}

	ret = _ssd1683_write_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
				SSD1683_UDC2_POWER_OFF_ANALOG);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}
	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}
	ret = _ssd1683_wait_busy(cfg, K_SECONDS(2), "power_off");
	if (ret < 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	data->is_powered_on = false;
	k_mutex_unlock(&data->lock);
	return 0;
}

int ssd1683_hibernate(const struct device *dev)
{
	const struct ssd1683_config *cfg;
	struct ssd1683_data *data;
	int ret;

	if (!dev) {
		return -EINVAL;
	}

	cfg = dev->config;
	data = dev->data;

	/* Drain pending refresh without holding the bus mutex (F19). */
	ret = ssd1683_flush(dev, K_SECONDS(5));
	if (ret < 0) {
		LOG_WRN("hibernate: flush drain: %d", ret);
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->is_powered_on) {
		ret = _ssd1683_write_u8(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL_2,
					SSD1683_UDC2_POWER_OFF_ANALOG);
		if (ret < 0) {
			k_mutex_unlock(&data->lock);
			return ret;
		}
		ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
		if (ret < 0) {
			k_mutex_unlock(&data->lock);
			return ret;
		}
		ret = _ssd1683_wait_busy(cfg, K_SECONDS(2), "power_off");
		if (ret < 0) {
			k_mutex_unlock(&data->lock);
			return ret;
		}
		data->is_powered_on = false;
	}

	if (data->is_initialized) {
		ret = _ssd1683_write_u8(cfg, SSD1683_CMD_DEEP_SLEEP, 0x01);
		if (ret < 0) {
			k_mutex_unlock(&data->lock);
			return ret;
		}
	}

	data->is_initialized = false;
	data->is_powered_on = false;
	data->is_blanked = true;
	data->needs_full_sync = true;
	k_mutex_unlock(&data->lock);
	return 0;
}

int ssd1683_set_fast_update(const struct device *dev, bool fast_update)
{
	struct ssd1683_data *data;

	if (!dev) {
		return -EINVAL;
	}

	data = dev->data;
	data->use_fast_update = fast_update;
	return 0;
}

bool ssd1683_is_powered_on(const struct device *dev)
{
	struct ssd1683_data *data;

	if (!dev) {
		return false;
	}

	data = dev->data;
	return data->is_powered_on;
}

bool ssd1683_is_initialized(const struct device *dev)
{
	struct ssd1683_data *data;

	if (!dev) {
		return false;
	}

	data = dev->data;
	return data->is_initialized;
}

static int _ssd1683_bringup_nolock(const struct device *dev, bool preserve_shadow)
{
	const struct ssd1683_config *cfg = dev->config;
	struct ssd1683_data *data = dev->data;
	bool controller_touched;
	int ret;

	ret = _ssd1683_gpio_configure(dev);
	if (ret < 0) {
		data->is_initialized = false;
		data->is_powered_on = false;
		return ret;
	}

	if (!preserve_shadow) {
		data->is_powered_on = false;
		data->is_blanked = false;
		data->force_full = false;
		data->use_fast_update = cfg->fast_mode;
		data->partial_count = 0;
		data->dirty = (struct ssd1683_dirty_rect){ 0 };
		memset(cfg->shadow_fb, 0xFF, cfg->shadow_fb_size);
	}

	controller_touched = false;
	ret = _ssd1683_reset(cfg);
	if (ret < 0) {
		goto fail;
	}
	k_msleep(10);

	ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_SWRESET);
	if (ret < 0) {
		goto fail;
	}
	controller_touched = true;

	ret = _ssd1683_wait_busy(cfg, K_SECONDS(2), "swreset");
	if (ret < 0) {
		goto fail;
	}
	k_msleep(10);

	for (size_t i = 0; i < ARRAY_SIZE(k_init_fixed); i++) {
		const struct ssd1683_init_step *s = &k_init_fixed[i];

		ret = _ssd1683_write_cmd(cfg, s->cmd);
		if (ret < 0) {
			goto fail;
		}
		ret = _ssd1683_write_data(cfg, s->data, s->len);
		if (ret < 0) {
			goto fail;
		}
	}

	{
		uint16_t mux = cfg->height - 1;
		uint8_t mux_data[3] = {
			(uint8_t)(mux & 0xFF),
			(uint8_t)((mux >> 8) & 0xFF),
			0x00,
		};

		ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DRIVER_OUTPUT_CTRL);
		if (ret < 0) {
			goto fail;
		}
		ret = _ssd1683_write_data(cfg, mux_data, sizeof(mux_data));
		if (ret < 0) {
			goto fail;
		}
	}

	ret = _ssd1683_set_partial_ram_area(cfg, 0, 0, cfg->width, cfg->height);
	if (ret < 0) {
		goto fail;
	}

	if (!preserve_shadow) {
		data->needs_full_sync = true;
	}

	data->is_initialized = true;
	data->is_powered_on = false;

	LOG_INF("registered: %ux%u, fast=%d, preserve_shadow=%d", cfg->width,
		cfg->height, (int)data->use_fast_update, (int)preserve_shadow);
	return 0;

fail:
	if (controller_touched) {
		(void)_ssd1683_write_u8(cfg, SSD1683_CMD_DEEP_SLEEP, 0x01);
	}
	data->is_initialized = false;
	data->is_powered_on = false;
	if (!preserve_shadow) {
		data->needs_full_sync = true;
	}
	LOG_ERR("bringup failed: %d", ret);
	return ret;
}

int _ssd1683_bringup(const struct device *dev, bool preserve_shadow)
{
	struct ssd1683_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = _ssd1683_bringup_nolock(dev, preserve_shadow);
	k_mutex_unlock(&data->lock);
	return ret;
}
