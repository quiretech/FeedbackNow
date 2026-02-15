
/*
 * Battery ADC helper.
 *
 * Refactored from standalone adc main() into a reusable module. Provides:
 *  - battery_adc_init(): one-time setup of ADC channel (AIN3 on nRF52840DK)
 *  - battery_adc_read_mv(): synchronous read returning battery voltage (mV)
 *
 * System-mode / housekeeping can call battery_adc_read_mv() under 3.3A rail
 * to build heartbeat payloads (EVT_BATTERY_STATUS) without owning ADC
 * hardware details.
 */

#include <hal/nrf_saadc.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "battery_adc.h"

LOG_MODULE_REGISTER(battery_adc, LOG_LEVEL_INF);

#define ADC_NODE DT_NODELABEL(adc)
#define ADC_RESOLUTION 10
#define ADC_CHANNEL_ID                                                         \
  3 /* AIN3 on nRF52840; match reference (channel 0, BIT(0)) */
#define ADC_RAW_MAX                                                            \
  1023 /* 10-bit ADC valid range; values outside are hardware garbage */

/* Simple resistor divider gain from board: approx 3.120x from pin to VBAT. */
#define BATTERY_DIVIDER_NUM 2956
#define BATTERY_DIVIDER_DEN 1000
#define ADC_SAMPLES 20
#define ADC_SAMPLE_DELAY_MS 100 /* match reference: 100 ms between samples */

/* nRF internal reference 0.6 V; use if adc_ref_internal() returns 0 at read
 * time */
#define ADC_REF_INTERNAL_MV 600

static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);
static uint16_t cached_ref_mv;

static int16_t sample_buffer;

static struct adc_channel_cfg channel_cfg = {
    .gain = ADC_GAIN_1_3,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
    .channel_id = ADC_CHANNEL_ID,
    .input_positive = NRF_SAADC_INPUT_AIN3, /* AIN3 = P0.05 */
};

static struct adc_sequence sequence = {
    .channels = BIT(ADC_CHANNEL_ID),
    .buffer = &sample_buffer,
    .buffer_size = sizeof(sample_buffer),
    .resolution = ADC_RESOLUTION,
};

int battery_adc_init(void) {
  int ret;

  if (!device_is_ready(adc_dev)) {
    LOG_ERR("ADC device not ready");
    return -ENODEV;
  }

  ret = adc_channel_setup(adc_dev, &channel_cfg);
  if (ret < 0) {
    LOG_ERR("ADC channel setup failed (%d)", ret);
    return ret;
  }

  cached_ref_mv = adc_ref_internal(adc_dev);
  if (cached_ref_mv == 0) {
    cached_ref_mv = ADC_REF_INTERNAL_MV;
    LOG_WRN("ADC internal ref not available, using %u mV", cached_ref_mv);
  } else {
    LOG_INF("ADC internal reference voltage: %u mV", cached_ref_mv);
  }
  nrf_saadc_task_trigger(NRF_SAADC, NRF_SAADC_TASK_CALIBRATEOFFSET);
  k_msleep(20);
  LOG_INF("ADC initialized and calibrated");

  return 0;
}

int battery_adc_read_mv(int32_t *battery_mv) {
  if (battery_mv == NULL) {
    return -EINVAL;
  }

  battery_adc_init();

  /* Discard read: clear result register and let ADC settle (reference does
   * one discard then 100 ms before first real read). */
  (void)adc_read(adc_dev, &sequence);
  k_msleep(100);

  uint16_t ref_mv = adc_ref_internal(adc_dev);
  if (ref_mv == 0) {
    ref_mv = cached_ref_mv;
  }

  int32_t battery_mv_sum = 0;
  int valid_reads = 0;
  int16_t first_raw = 0;
  bool logged_first = false;

  for (int i = 0; i < ADC_SAMPLES; i++) {
    k_msleep(ADC_SAMPLE_DELAY_MS);
    int ret = adc_read(adc_dev, &sequence);
    if (ret != 0) {
      continue;
    }
    if (sample_buffer < 0 || sample_buffer > ADC_RAW_MAX) {
      LOG_DBG("ADC raw out of range, skip: %d", (int)sample_buffer);
      continue;
    }

    if (!logged_first) {
      first_raw = sample_buffer;
      logged_first = true;
    }

    int32_t pin_mv = (int32_t)sample_buffer;
    ret = adc_raw_to_millivolts(ref_mv, channel_cfg.gain, ADC_RESOLUTION,
                                &pin_mv);
    if (ret != 0) {
      continue;
    }
    int32_t vbat = (pin_mv * BATTERY_DIVIDER_NUM) / BATTERY_DIVIDER_DEN;
    battery_mv_sum += vbat;
    valid_reads++;
  }

  if (valid_reads == 0) {
    LOG_WRN("ADC: no valid readings");
    return -EIO;
  }

  *battery_mv = battery_mv_sum / valid_reads;
  LOG_INF("ADC ref=%u mV raw_first=%d valid=%d battery_mv=%d", ref_mv,
          first_raw, valid_reads, *battery_mv);
  return 0;
}
