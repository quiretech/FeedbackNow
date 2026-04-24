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

/* DISPLAY_UPDATE_CTRL_2 (0x22) payload options.  The payload bitfield
 * enables/disables specific phases of the update sequence. */
#define SSD1683_UDC2_POWER_ON              0xE0 /* en clk, en analog, disp off */
#define SSD1683_UDC2_FULL_SLOW             0xF7 /* en clk, en analog, LUT from OTP, display, dis analog, dis clk */
#define SSD1683_UDC2_FULL_FAST             0xD7 /* skip LUT load, faster waveform (needs 0x1A temp write) */
#define SSD1683_UDC2_PARTIAL               0xFC /* differential update using RAM-B as previous frame */
#define SSD1683_UDC2_POWER_OFF_ANALOG      0x83 /* display, disable analog + clock */

struct ssd1683_config {
    struct spi_dt_spec bus;
    struct gpio_dt_spec dc;
    struct gpio_dt_spec rst;
    struct gpio_dt_spec busy;
    uint16_t width;
    uint16_t height;
    bool fast_mode;

    /* Per-instance resources provided by the DEVICE_DT_DEFINE macro. */
    uint8_t *shadow_fb;           /* WIDTH*HEIGHT/8 bytes, MSB-first mono */
    size_t   shadow_fb_size;
    struct k_work_q *workq;
    k_thread_stack_t *workq_stack;
    size_t workq_stack_size;
};

struct ssd1683_dirty_rect {
    uint16_t x, y, w, h; /* byte-aligned on X, w = 0 means empty */
};

struct ssd1683_data {
    const struct device *self;

    bool is_initialized;
    bool is_powered_on;
    bool is_blanked;
    bool use_fast_update;
    bool force_full;

    uint32_t partial_count;
    struct ssd1683_dirty_rect dirty;

    struct k_mutex lock;
    struct k_sem refresh_done;
    struct k_work_delayable refresh_work;
};

/**
 * @brief Force pending coalesced writes to be flushed to the panel now and
 *        block until the refresh sequence completes.
 *
 * Safe to call from any thread except the driver's own workqueue thread.
 *
 * @param dev Device pointer.
 * @param timeout Max time to wait for the refresh to finish.
 * @return 0 on success, -EAGAIN on timeout, negative errno on failure.
 */
int ssd1683_flush(const struct device *dev, k_timeout_t timeout);

/**
 * @brief Query whether a refresh is currently in flight or scheduled.
 */
bool ssd1683_is_busy(const struct device *dev);

/**
 * @brief Request that the next flush performs a full refresh instead of
 *        partial, to clear accumulated ghosting.
 */
int ssd1683_force_full_refresh(const struct device *dev);

/**
 * @brief Fill the shadow framebuffer with @p value and force a full refresh.
 *        Useful at boot or whenever the app wants a clean wipe.
 *
 * @param value 0x00 (black) or 0xFF (white).
 */
int ssd1683_clear_screen(const struct device *dev, uint8_t value);

/**
 * @brief Power on / off / hibernate the EPD analog section.
 * These map to DISPLAY_UPDATE_CTRL_2 sequences and (for hibernate) to the
 * DEEP_SLEEP command.  They are primarily used by the display API wrapper
 * (blanking_off / blanking_on) but are exported for direct use too.
 */
int ssd1683_power_on(const struct device *dev);
int ssd1683_power_off(const struct device *dev);
int ssd1683_hibernate(const struct device *dev);

/**
 * @brief Enable or disable the fast full-refresh waveform.  Fast mode skips
 *        the LUT-from-OTP load and shortens the update by ~30%.  Default on.
 */
int ssd1683_set_fast_update(const struct device *dev, bool fast_update);

bool ssd1683_is_powered_on(const struct device *dev);
bool ssd1683_is_initialized(const struct device *dev);

#endif /* SSD1683_H */
