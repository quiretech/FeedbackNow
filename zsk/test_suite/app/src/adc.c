
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
#define ADC_CHANNEL_ID 3 /* AIN3 = P0.05 */

/* Simple resistor divider gain from board: approx 3.120x from pin to VBAT. */
#define BATTERY_DIVIDER_NUM 2956
#define BATTERY_DIVIDER_DEN 1000

static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

static int16_t sample_buffer;

static struct adc_channel_cfg channel_cfg = {
    .gain = ADC_GAIN_1_3,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 20),
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

  uint16_t ref_mv = adc_ref_internal(adc_dev);

  if (ref_mv > 0) {
    LOG_INF("ADC internal reference voltage: %u mV", ref_mv);
  } else {
    LOG_WRN("ADC internal reference voltage not available");
  }
  nrf_saadc_task_trigger(NRF_SAADC, NRF_SAADC_TASK_CALIBRATEOFFSET);

  LOG_INF("ADC initialized and calibrated");

  return 0;
}

int battery_adc_read_mv(int32_t *battery_mv) {
  if (battery_mv == NULL) {
    return -EINVAL;
  }

  int32_t millivolts = 0;
  int ret = adc_read(adc_dev, &sequence);
  if (ret < 0) {
    LOG_ERR("ADC read failed (%d)", ret);
    return ret;
  }

  millivolts = (int32_t)sample_buffer;

  /* Convert raw value to millivolts at the ADC pin. */
  ret = adc_raw_to_millivolts(adc_ref_internal(adc_dev), channel_cfg.gain,
                              ADC_RESOLUTION, &millivolts);
  if (ret < 0) {
    LOG_WRN("Raw to mV conversion not supported");
    return ret;
  }

  /* Scale up to battery voltage using board-specific divider. */
  *battery_mv = (millivolts * BATTERY_DIVIDER_NUM) / BATTERY_DIVIDER_DEN;
  LOG_DBG("ADC raw=%d, pin_mv=%d, battery_mv=%d", sample_buffer, millivolts,
          *battery_mv);
  return 0;
}
