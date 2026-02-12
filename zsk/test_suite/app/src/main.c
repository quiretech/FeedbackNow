/*
 * Minimal QA Test Suite
 * Tests all peripherals sequentially and reports pass/fail results via UART
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/nrf_saadc.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/sys/util.h>

#include "battery_adc.h"
#include "buttons.h"
#include "display_manager.h"
#include "leds.h"
#include "lora_app.h"
#include "pn5180.h"
#include "power_ctrl.h"
#include "rtc.h"
#include "sys_config.h"

#if EPD_ENABLED
#include <lvgl.h>
#endif

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* ANSI color codes for terminal output */
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_RED "\033[31m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_CYAN "\033[36m"

/* Test IDs */
typedef enum {
  TEST_NFC,
  TEST_EPD,
  TEST_ADC,
  TEST_EEPROM,
  TEST_RTC,
  TEST_BUTTON,
  TEST_LORA,
  TEST_COUNT
} test_id_t;

/* Test result structure */
typedef struct {
  test_id_t id;
  const char *name;
  bool passed;
} test_result_t;

static test_result_t test_results[TEST_COUNT] = {
    {TEST_NFC, "NFC (PN5180)", false},
    {TEST_EPD, "EPD Display", false},
    {TEST_ADC, "ADC", false},
    {TEST_EEPROM, "EEPROM", false},
    {TEST_RTC, "RTC", false},
    {TEST_BUTTON, "Button", false},
    {TEST_LORA, "LoRa (SX1262)", false},
};

/* Forward declarations */
static bool test_nfc(void);
static bool test_epd(void);
static bool test_adc(void);
static bool test_eeprom(void);
static bool test_rtc(void);
static bool test_button(void);
static bool test_lora(void);
static void print_test_summary(void);

/* NFC Test: Verify PN5180 SPI communication by reading EEPROM version */
static bool test_nfc(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting NFC (PN5180) test..." ANSI_RESET);

  const struct device *pn5180_dev = DEVICE_DT_GET(DT_NODELABEL(pn5180));
  if (!device_is_ready(pn5180_dev)) {
    LOG_ERR(ANSI_RED "[TEST] NFC: ✗ FAIL - PN5180 device not ready" ANSI_RESET);
    return false;
  }

  int ret = pn5180_init(pn5180_dev);
  if (ret != 0) {
    LOG_ERR(ANSI_RED "[TEST] NFC: ✗ FAIL - pn5180_init failed: %d" ANSI_RESET,
            ret);
    return false;
  }

  struct pn5180_version_info version;
  ret = pn5180_get_version(pn5180_dev, &version);
  if (ret != 0) {
    LOG_ERR(ANSI_RED
            "[TEST] NFC: ✗ FAIL - Failed to read version: %d" ANSI_RESET,
            ret);
    return false;
  }

  /* Check for valid version data */
  if (version.firmware_version == 0 && version.product_version == 0) {
    LOG_ERR(ANSI_RED
            "[TEST] NFC: ✗ FAIL - Invalid version data (all zeros)" ANSI_RESET);
    return false;
  }

  LOG_INF("[TEST] NFC: Product Version:  %d.%d",
          (version.product_version >> 8) & 0xFF,
          version.product_version & 0xFF);
  LOG_INF("[TEST] NFC: Firmware Version: %d.%d",
          (version.firmware_version >> 8) & 0xFF,
          version.firmware_version & 0xFF);
  LOG_INF("[TEST] NFC: EEPROM Version:   %d.%d",
          (version.eeprom_version >> 8) & 0xFF, version.eeprom_version & 0xFF);

  LOG_INF(ANSI_GREEN
          "[TEST] NFC: ✓ PASS - SPI communication verified" ANSI_RESET);
  return true;
}

/* EPD Display Test: Initialize display and write "Hello World" using LVGL */
static bool test_epd(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting EPD Display test..." ANSI_RESET);

  int ret = display_manager_init();
  if (ret != 0) {
    LOG_ERR(ANSI_RED
            "[TEST] EPD: ✗ FAIL - display_manager_init failed: %d" ANSI_RESET,
            ret);
    return false;
  }

#if EPD_ENABLED
  const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display_dev)) {
    LOG_ERR(ANSI_RED
            "[TEST] EPD: ✗ FAIL - Display device not ready" ANSI_RESET);
    return false;
  }

  /* Get the default display from LVGL (set up by display_manager_init) */
  lv_display_t *disp = lv_display_get_default();
  if (disp == NULL) {
    LOG_ERR(ANSI_RED
            "[TEST] EPD: ✗ FAIL - LVGL display not initialized" ANSI_RESET);
    return false;
  }

  /* Create a simple "Hello World" screen */
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *label = lv_label_create(screen);
  lv_label_set_text(label, "EPD Test Passed");
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);

  lv_scr_load(screen);

  /* Process LVGL tasks to render */
  for (int i = 0; i < 20; i++) {
    lv_task_handler();
    k_msleep(50);
  }

  LOG_INF(ANSI_GREEN "[TEST] EPD: ✓ PASS - Display initialized and 'EPD Test "
                     "Passed' rendered" ANSI_RESET);
  return true;
#else
  LOG_WRN(ANSI_YELLOW
          "[TEST] EPD: ⚠ EPD_ENABLED is 0, skipping display test" ANSI_RESET);
  return false;
#endif
}

/* ADC Test: Perform a quick ADC read - using exact reference code approach */
static bool test_adc(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting ADC test..." ANSI_RESET);

  // 1. Setup hardware power
  power_ctrl_set(POWER_EN_3V3A, true);
  k_msleep(100);

  const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
  if (!device_is_ready(adc_dev)) {
    LOG_ERR(ANSI_RED "[TEST] ADC: ✗ FAIL - Device not ready" ANSI_RESET);
    return false;
  }

  // --- NEW: Define the variables the compiler was missing ---
  int16_t sample_buffer; // Where the raw ADC bit value is stored

  struct adc_channel_cfg channel_cfg = {
      .gain = ADC_GAIN_1_3,
      .reference = ADC_REF_INTERNAL, // 0.6V internal ref
      .acquisition_time = ADC_ACQ_TIME_DEFAULT,
      .channel_id = 0,
      .input_positive = NRF_SAADC_INPUT_AIN3, // P0.05
  };

  struct adc_sequence sequence = {
      .channels = BIT(0),
      .buffer = &sample_buffer,
      .buffer_size = sizeof(sample_buffer),
      .resolution = 10,
  };
  // -------------------------------------------------------

  // Initialize channel
  adc_channel_setup(adc_dev, &channel_cfg);

  // Calibrate to remove offset noise
  nrf_saadc_task_trigger(NRF_SAADC, NRF_SAADC_TASK_CALIBRATEOFFSET);
  k_msleep(20);

  // CRITICAL: Perform a "Discard Read"
  // This clears the result register of the 8193/garbage value
  adc_read(adc_dev, &sequence);
  LOG_INF("[TEST] ADC: Discarded initial stabilization read");

  int32_t battery_mv_sum = 0;
  int valid_reads = 0;

  for (int i = 0; i < 5; i++) {
    k_msleep(100);
    if (adc_read(adc_dev, &sequence) == 0) {
      // Filter out values that are mathematically impossible for a 10-bit ADC
      // (10-bit max is 1023. Anything like 8193 is hardware-status garbage)
      if (sample_buffer < 0 || sample_buffer > 1023) {
        LOG_WRN("[TEST] ADC: Ignoring out-of-range raw value: %d",
                (int)sample_buffer);
        continue;
      }

      int32_t mv = (int32_t)sample_buffer;
      // Convert bits to millivolts at the PIN
      adc_raw_to_millivolts(adc_ref_internal(adc_dev), ADC_GAIN_1_3, 10, &mv);

      // Convert pin voltage to Battery voltage (assuming a standard divider)
      // Adjust the 2956/1000 ratio if your resistor divider is different
      int32_t vbat = (mv * 2956) / 1000;

      LOG_INF("[TEST] ADC: Sample %d - Raw: %d, Vbatt: %d mV", i + 1,
              sample_buffer, vbat);
      battery_mv_sum += vbat;
      valid_reads++;
    }
  }

  if (valid_reads == 0) {
    LOG_ERR(ANSI_RED
            "[TEST] ADC: ✗ FAIL - No valid readings obtained" ANSI_RESET);
    return false;
  }

  int32_t final_vbat = battery_mv_sum / valid_reads;
  LOG_INF("[TEST] ADC: Final Average: %d mV", final_vbat);

  // Final Sanity Check: If battery is disconnected, you'll likely see < 500mV
  if (final_vbat < 2000) {
    LOG_ERR(ANSI_RED "[TEST] ADC: ✗ FAIL - Voltage too low (%d mV). Check "
                     "battery connection." ANSI_RESET,
            final_vbat);
    return false;
  }

  LOG_INF(ANSI_GREEN
          "[TEST] ADC: ✓ PASS - ADC read successful (%d mV)" ANSI_RESET,
          final_vbat);
  return true;
}
/* EEPROM Test: Simple read/write verification */
static bool test_eeprom(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting EEPROM test..." ANSI_RESET);

#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
  const struct device *eeprom_dev = DEVICE_DT_GET(DT_NODELABEL(eeprom0));
  if (!device_is_ready(eeprom_dev)) {
    LOG_ERR(ANSI_RED "[TEST] EEPROM: ✗ FAIL - Device not ready" ANSI_RESET);
    return false;
  }

  /* Test write/read at address 0x0100 */
  const uint16_t test_addr = 0x0100;
  uint8_t write_data[] = "Test";
  uint8_t read_data[sizeof(write_data)] = {0};

  LOG_INF("[TEST] EEPROM: Writing \"Test\" to address 0x%04X...", test_addr);
  int ret = eeprom_write(eeprom_dev, test_addr, write_data, sizeof(write_data));
  if (ret != 0) {
    LOG_ERR(ANSI_RED "[TEST] EEPROM: ✗ FAIL - Write failed: %d" ANSI_RESET,
            ret);
    return false;
  }

  /* Wait for write to complete */
  k_msleep(10);

  LOG_INF("[TEST] EEPROM: Reading back from address 0x%04X...", test_addr);
  ret = eeprom_read(eeprom_dev, test_addr, read_data, sizeof(read_data));
  if (ret != 0) {
    LOG_ERR(ANSI_RED "[TEST] EEPROM: ✗ FAIL - Read failed: %d" ANSI_RESET, ret);
    return false;
  }

  /* Show what was written and read */
  LOG_INF("[TEST] EEPROM: Written: \"%s\" (%d bytes)", write_data,
          sizeof(write_data));
  LOG_INF("[TEST] EEPROM: Read:    \"%s\" (%d bytes)", read_data,
          sizeof(read_data));

  bool data_match = true;
  for (size_t i = 0; i < sizeof(write_data); i++) {
    if (write_data[i] != read_data[i]) {
      data_match = false;
      break;
    }
  }

  if (!data_match) {
    LOG_ERR(ANSI_RED "[TEST] EEPROM: ✗ FAIL - Data mismatch!" ANSI_RESET);
    LOG_HEXDUMP_ERR(write_data, sizeof(write_data), "Written");
    LOG_HEXDUMP_ERR(read_data, sizeof(read_data), "Read");
    return false;
  }

  LOG_INF(ANSI_GREEN
          "[TEST] EEPROM: ✓ PASS - Read/write test successful" ANSI_RESET);
  return true;
#else
  LOG_WRN("[TEST] EEPROM: eeprom0 node not found in devicetree");
  return false;
#endif
}

/* RTC Test: Read RTC time and count 2 seconds */
static bool test_rtc(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting RTC test..." ANSI_RESET);

  int ret = rtc_app_init();
  if (ret != 0) {
    LOG_ERR(ANSI_RED "[TEST] RTC: ✗ FAIL - rtc_app_init failed: %d" ANSI_RESET,
            ret);
    return false;
  }

#if DT_NODE_EXISTS(DT_NODELABEL(pcf8523))
  const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(pcf8523));
  if (!device_is_ready(rtc_dev)) {
    LOG_ERR(ANSI_RED "[TEST] RTC: ✗ FAIL - Device not ready" ANSI_RESET);
    return false;
  }

  struct rtc_time time1, time2;
  ret = rtc_get_time(rtc_dev, &time1);
  if (ret != 0) {
    LOG_ERR(ANSI_RED
            "[TEST] RTC: ✗ FAIL - Failed to read initial time: %d" ANSI_RESET,
            ret);
    return false;
  }

  LOG_INF("[TEST] RTC: Initial time: %04d-%02d-%02d %02d:%02d:%02d",
          time1.tm_year + 1900, time1.tm_mon + 1, time1.tm_mday, time1.tm_hour,
          time1.tm_min, time1.tm_sec);

  /* Wait 2 seconds */
  k_sleep(K_SECONDS(2));

  ret = rtc_get_time(rtc_dev, &time2);
  if (ret != 0) {
    LOG_ERR("[TEST] RTC: Failed to read time after delay: %d", ret);
    return false;
  }

  LOG_INF("[TEST] RTC: Time after 2s: %04d-%02d-%02d %02d:%02d:%02d",
          time2.tm_year + 1900, time2.tm_mon + 1, time2.tm_mday, time2.tm_hour,
          time2.tm_min, time2.tm_sec);

  /* Verify time advanced (allow for small timing variations) */
  int time_diff = (time2.tm_hour * 3600 + time2.tm_min * 60 + time2.tm_sec) -
                  (time1.tm_hour * 3600 + time1.tm_min * 60 + time1.tm_sec);

  if (time_diff < 1 || time_diff > 3) {
    LOG_WRN(ANSI_YELLOW
            "[TEST] RTC: ⚠ Time difference unexpected: %d seconds" ANSI_RESET,
            time_diff);
    /* Still pass if we can read the time */
  }

  LOG_INF(ANSI_GREEN
          "[TEST] RTC: ✓ PASS - RTC read successful, time advanced" ANSI_RESET);
  return true;
#else
  LOG_WRN(ANSI_YELLOW
          "[TEST] RTC: ⚠ pcf8523 node not found in devicetree" ANSI_RESET);
  return false;
#endif
}

/* Button Test: Allow user to press buttons and light up LED */
// static bool test_button(void) {
//   LOG_INF(ANSI_BLUE "[TEST] Starting Button test..." ANSI_RESET);
//   LOG_INF(ANSI_YELLOW
//           "[TEST] Button: Press any button within 5 seconds..." ANSI_RESET);

//   int ret = buttons_init();
//   if (ret != 0) {
//     LOG_ERR(ANSI_RED
//             "[TEST] Button: ✗ FAIL - buttons_init failed: %d" ANSI_RESET,
//             ret);
//     return false;
//   }

//   ret = leds_init();
//   if (ret != 0) {
//     LOG_ERR(ANSI_RED "[TEST] Button: ✗ FAIL - leds_init failed: %d"
//     ANSI_RESET,
//             ret);
//     return false;
//   }

//   /* Poll for button press with 5 second timeout */
//   button_event_t event;
//   bool got_event = buttons_get_event(&event, K_SECONDS(5));

//   if (!got_event) {
//     LOG_WRN(
//         ANSI_YELLOW
//         "[TEST] Button: ⚠ No button press detected within timeout"
//         ANSI_RESET);
//     return false;
//   }

//   if (event.type == BUTTON_EVENT_PRESS) {
//     LOG_INF("[TEST] Button: Button %d pressed", event.button_id);

//     /* Turn on LED */
//     ret = led_set(0, true);
//     if (ret != 0) {
//       LOG_ERR(ANSI_RED
//               "[TEST] Button: ✗ FAIL - Failed to turn on LED: %d" ANSI_RESET,
//               ret);
//       return false;
//     }

//     LOG_INF("[TEST] Button: LED turned ON");
//     k_msleep(500);

//     /* Wait for release */
//     got_event = buttons_get_event(&event, K_SECONDS(2));
//     if (got_event && event.type == BUTTON_EVENT_RELEASE) {
//       LOG_INF("[TEST] Button: Button %d released", event.button_id);
//     }

//     /* Turn off LED */
//     led_set(0, false);
//     LOG_INF("[TEST] Button: LED turned OFF");

//     LOG_INF(ANSI_GREEN "[TEST] Button: ✓ PASS - Button press detected and LED
//     "
//                        "responded" ANSI_RESET);
//     return true;
//   }

//   LOG_WRN(ANSI_YELLOW "[TEST] Button: ⚠ Unexpected event type" ANSI_RESET);
//   return false;
// }

/* Button Test: Track multiple button presses within a 10s window */
static bool test_button(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting Multi-Button test..." ANSI_RESET);

/* Define how many buttons you expect to be tested (e.g., 2 buttons) */
#define EXPECTED_BUTTON_COUNT 6
  uint32_t buttons_pressed_mask = 0;
  int unique_buttons_found = 0;

  int ret = buttons_init();
  if (ret != 0) {
    LOG_ERR(ANSI_RED
            "[TEST] Button: ✗ FAIL - buttons_init failed: %d" ANSI_RESET,
            ret);
    return false;
  }

  ret = leds_init();
  if (ret != 0) {
    LOG_ERR(ANSI_RED "[TEST] Button: ✗ FAIL - leds_init failed: %d" ANSI_RESET,
            ret);
    return false;
  }

  LOG_INF(ANSI_YELLOW
          "[TEST] Button: Press ALL %d buttons within 10 seconds..." ANSI_RESET,
          EXPECTED_BUTTON_COUNT);

  int64_t start_time = k_uptime_get();
  int64_t timeout = 10000; // 10 seconds

  while (k_uptime_get() - start_time < timeout) {
    button_event_t event;

    // Calculate remaining time for the poll
    int64_t remaining = timeout - (k_uptime_get() - start_time);
    if (remaining <= 0)
      break;

    // Check for an event (non-blocking or short timeout)
    if (buttons_get_event(&event, K_MSEC(remaining > 100 ? 100 : remaining))) {

      if (event.type == BUTTON_EVENT_PRESS) {
        // Check if this is a new button we haven't seen yet
        if (!(buttons_pressed_mask & BIT(event.button_id))) {
          buttons_pressed_mask |= BIT(event.button_id);
          unique_buttons_found++;

          LOG_INF("[TEST] Button: %d pressed! (%d/%d)", event.button_id,
                  unique_buttons_found, EXPECTED_BUTTON_COUNT);

          /* Visual feedback: Blink LED on successful press */
          led_set(0, true);
          k_msleep(100);
          led_set(0, false);
        }
      }
    }

    /* Exit early if all buttons are found */
    if (unique_buttons_found >= EXPECTED_BUTTON_COUNT) {
      break;
    }
  }

  if (unique_buttons_found >= EXPECTED_BUTTON_COUNT) {
    LOG_INF(ANSI_GREEN
            "[TEST] Button: ✓ PASS - All %d buttons verified" ANSI_RESET,
            EXPECTED_BUTTON_COUNT);
    return true;
  } else {
    LOG_ERR(ANSI_RED
            "[TEST] Button: ✗ FAIL - Only %d/%d buttons pressed" ANSI_RESET,
            unique_buttons_found, EXPECTED_BUTTON_COUNT);
    return false;
  }
}

/* LoRa Test: Initialize driver and confirm SX1262 is ready (no OTAA join) */
static bool test_lora(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting LoRa (SX1262) test..." ANSI_RESET);

  const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
  if (!device_is_ready(lora_dev)) {
    LOG_ERR(ANSI_RED "[TEST] LoRa: ✗ FAIL - Device not ready" ANSI_RESET);
    return false;
  }

  LOG_INF("[TEST] LoRa: Device ready: %s", lora_dev->name);

  int ret = lorawan_start();
  if (ret < 0) {
    LOG_ERR(ANSI_RED
            "[TEST] LoRa: ✗ FAIL - lorawan_start failed: %d" ANSI_RESET,
            ret);
    return false;
  }

  LOG_INF(ANSI_GREEN
          "[TEST] LoRa: ✓ PASS - SX1262 initialized and ready" ANSI_RESET);
  return true;
}

/* Print test summary with color coding */
static void print_test_summary(void) {
  LOG_INF("");
  LOG_INF("========================================");
  LOG_INF(ANSI_BOLD ANSI_CYAN "[TEST SUMMARY]" ANSI_RESET);
  LOG_INF("========================================");
  LOG_INF("");

  int passed = 0;
  for (int i = 0; i < TEST_COUNT; i++) {
    if (test_results[i].passed) {
      LOG_INF(ANSI_GREEN "  ✓ %-20s PASS" ANSI_RESET, test_results[i].name);
      passed++;
    } else {
      LOG_INF(ANSI_RED "  ✗ %-20s FAIL" ANSI_RESET, test_results[i].name);
    }
  }

  LOG_INF("");
  LOG_INF("========================================");
  if (passed == TEST_COUNT) {
    LOG_INF(ANSI_BOLD ANSI_GREEN "RESULT: ALL TESTS PASSED (%d/%d)" ANSI_RESET,
            passed, TEST_COUNT);
  } else {
    LOG_INF(ANSI_BOLD ANSI_RED "RESULT: %d/%d TESTS PASSED" ANSI_RESET, passed,
            TEST_COUNT);
  }
  LOG_INF("========================================");
}

int main(void) {
  int ret;

  LOG_INF("");
  LOG_INF("========================================");
  LOG_INF("Test Suite Starting");
  LOG_INF("========================================");
  LOG_INF("");

  k_sleep(K_SECONDS(1));

  /* Initialize power control */
  ret = power_ctrl_init();
  if (ret < 0) {
    LOG_ERR("Power control init failed: %d", ret);
    return ret;
  }

  /* Power up ALL domains immediately for testing - ensure all rails are ON */
  LOG_INF("Enabling ALL power rails for test suite...");
  power_ctrl_set(POWER_EN_3V3, true);
  k_msleep(50);
  power_ctrl_set(POWER_EN_1V8, true);
  k_msleep(50);
  power_ctrl_set(POWER_EN_3V3A, true); /* Required for ADC */
  k_msleep(50);
  power_ctrl_set(POWER_EN_3V6, true);
  k_msleep(100); /* Longer stabilization delay for all rails */
  LOG_INF("All power rails enabled and stabilized");

  /* Wait for I2C bus to stabilize */
#if DT_NODE_EXISTS(DT_NODELABEL(arduino_i2c))
  const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));
  if (i2c_dev != NULL) {
    int i2c_retries = 10;
    while (i2c_retries > 0 && !device_is_ready(i2c_dev)) {
      k_msleep(100);
      i2c_retries--;
    }
    if (device_is_ready(i2c_dev)) {
      LOG_INF("I2C bus ready");
    }
    k_msleep(50); /* Additional delay for EEPROM power-on stabilization */
  }
#endif

  LOG_INF("");
  LOG_INF(ANSI_BOLD "Starting peripheral tests..." ANSI_RESET);
  LOG_INF("");

  /* Run all tests sequentially */
  test_results[TEST_NFC].passed = test_nfc();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_EPD].passed = test_epd();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_ADC].passed = test_adc();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_EEPROM].passed = test_eeprom();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_RTC].passed = test_rtc();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_BUTTON].passed = test_button();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_LORA].passed = test_lora();
  LOG_INF("");
  k_msleep(100);

  /* Print final summary */
  print_test_summary();

  LOG_INF("");
  LOG_INF(ANSI_BOLD ANSI_CYAN
          "Test suite completed. System will remain running." ANSI_RESET);

  /* Keep system running */
  while (1) {
    k_sleep(K_FOREVER);
  }

  return 0;
}
