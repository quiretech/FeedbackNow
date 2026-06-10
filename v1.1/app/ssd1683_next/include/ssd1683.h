#ifndef SSD1683_H
#define SSD1683_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

/**
 * @brief SSD1683 E-Paper Display Driver
 *
 * Zephyr display driver for the Solomon SSD1683 e-paper controller.
 * The driver owns an in-RAM shadow framebuffer and a dedicated workqueue
 * so that multiple LVGL flush callbacks per frame collapse into a single
 * EPD refresh, and so that the hundreds of ms spent waiting on BUSY never
 * block the caller.
 */

/* Commands */
#define SSD1683_CMD_DRIVER_OUTPUT_CTRL      0x01
#define SSD1683_CMD_GATE_DRIVING_VOLTAGE    0x03
#define SSD1683_CMD_SOURCE_DRIVING_VOLTAGE  0x04
#define SSD1683_CMD_SOFT_START              0x0C
#define SSD1683_CMD_DEEP_SLEEP              0x10
#define SSD1683_CMD_DATA_ENTRY_MODE         0x11
#define SSD1683_CMD_SWRESET                 0x12
#define SSD1683_CMD_TEMP_SENSOR             0x18
#define SSD1683_CMD_MASTER_ACTIVATION       0x20
#define SSD1683_CMD_DISPLAY_UPDATE_CTRL     0x21
#define SSD1683_CMD_DISPLAY_UPDATE_CTRL_2   0x22
#define SSD1683_CMD_WRITE_RAM_CURRENT       0x24 /* RAM-A (new) */
#define SSD1683_CMD_WRITE_RAM_PREVIOUS      0x26 /* RAM-B (old) */
#define SSD1683_CMD_WRITE_VCOM              0x2C
#define SSD1683_CMD_WRITE_TEMP_REG          0x1A
#define SSD1683_CMD_BORDER_WAVEFORM         0x3C
#define SSD1683_CMD_SET_RAM_X               0x44
#define SSD1683_CMD_SET_RAM_Y               0x45
#define SSD1683_CMD_SET_RAM_X_COUNTER       0x4E
#define SSD1683_CMD_SET_RAM_Y_COUNTER       0x4F

/* DISPLAY_UPDATE_CTRL_2 (0x22) payload options. */
#define SSD1683_UDC2_POWER_ON              0xE0
#define SSD1683_UDC2_FULL_SLOW             0xF7
#define SSD1683_UDC2_FULL_FAST             0xD7
#define SSD1683_UDC2_PARTIAL               0xFC
#define SSD1683_UDC2_POWER_OFF_ANALOG      0x83

struct ssd1683_config {
	/* From DT via SPI_DT_SPEC_GET / GPIO_DT_SPEC_GET */
	struct spi_dt_spec bus;
	struct gpio_dt_spec dc;
	struct gpio_dt_spec rst;
	struct gpio_dt_spec busy;
	/* From DT_PROP(n, width/height/fast_mode) */
	uint16_t width;
	uint16_t height;
	bool fast_mode;
	/* Allocated by DEVICE_DT_DEFINE macro. Layout: width/8 bytes per row,
	 * MSB-first mono (1=white, 0=black), size = width*height/8. */
	uint8_t *shadow_fb;
	size_t shadow_fb_size;
	/* Per-instance workqueue for deferred flush */
	struct k_work_q *workq;
	k_thread_stack_t *workq_stack;
	size_t workq_stack_size;
};

struct ssd1683_dirty_rect {
	uint16_t x, y, w, h; /* w == 0 => empty; x byte-aligned, w multiple of 8 */
};

/**
 * Runtime state flags (Phase 2 state machine).
 *
 * is_initialized  — SW reset + register table programmed; cleared on
 *                   hibernate, cold-start failure, refresh failure.
 * is_powered_on   — Analog section awake; full refresh clears this
 *                   (waveform power-off); partial refresh sets true.
 * is_blanked      — blanking_on() was called; writes auto blanking_off().
 * needs_full_sync — Next flush must rewrite PREVIOUS RAM; set at init,
 *                   hibernate, cold-start, clear_screen.
 * force_full      — App requested full waveform via ssd1683_force_full_refresh().
 * use_fast_update — Fast full waveform (0xD7) vs slow OTP LUT (0xF7).
 */
struct ssd1683_data {
	const struct device *self;
	bool is_initialized;
	bool is_powered_on;
	bool is_blanked;
	bool needs_full_sync;
	bool force_full;
	bool use_fast_update;
	uint32_t partial_count;
	struct ssd1683_dirty_rect dirty;
	struct k_mutex lock;
	struct k_sem refresh_done;
	struct k_work_delayable refresh_work;
};

/**
 * @brief Push coalesced shadow-FB pixels to the panel and block until done.
 *
 * Not automatic: the application must call this after staging (e.g. once
 * per lv_task_handler() loop). display_write() only copies into the shadow
 * framebuffer and arms a deferred refresh; this function kicks the worker
 * immediately and waits on @p timeout.
 *
 * @return 0 on success, -EAGAIN on timeout, negative errno on failure.
 */
int ssd1683_flush(const struct device *dev, k_timeout_t timeout);

/**
 * @brief Request a full-panel waveform on the next flush (ghosting cleanup).
 *
 * Sets force_full; does not block. Pair with ssd1683_flush().
 */
int ssd1683_force_full_refresh(const struct device *dev);
bool ssd1683_is_busy(const struct device *dev);
int ssd1683_clear_screen(const struct device *dev, uint8_t value);
int ssd1683_power_on(const struct device *dev);
int ssd1683_power_off(const struct device *dev);
int ssd1683_hibernate(const struct device *dev);
int ssd1683_set_fast_update(const struct device *dev, bool fast_update);
bool ssd1683_is_powered_on(const struct device *dev);
bool ssd1683_is_initialized(const struct device *dev);

#endif /* SSD1683_H */
