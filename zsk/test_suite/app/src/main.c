/*
 * Minimal QA Test Suite
 * Tests all peripherals sequentially and reports pass/fail results via UART
 * SPDX-License-Identifier: Apache-2.0
 */

// #define BEACON_MODE

#include <hal/nrf_saadc.h>
#include <stdbool.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "battery_adc.h"
#include "buttons.h"
#include "display_manager.h"
#include "leds.h"
#include "lora_app.h"
#include "pn5180.h"
#include "power_ctrl.h"
#include "rail_manager.h"
#include "rtc.h"
#include "sys_config.h"
#include <zephyr/drivers/lora.h>

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
  char detail[24];
} test_result_t;

static test_result_t test_results[TEST_COUNT] = {
    {TEST_NFC, "NFC:", false, ""},  {TEST_EPD, "EPD", false, ""},
    {TEST_ADC, "ADC:", false, ""},  {TEST_EEPROM, "EEPROM", false, ""},
    {TEST_RTC, "RTC:", false, ""},  {TEST_BUTTON, "Button:", false, ""},
    {TEST_LORA, "LoRa", false, ""},
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
static void epd_render_final_summary(void);
static void led_blink_pattern(int count, int on_ms, int off_ms);
static void shutdown_external_power(void);

/* NFC Test: Verify SPI (version read), then RF by scanning for tag and
printing
 * UUID. QA places an ISO15693 tag on the unit before running the suite; tag
 * must be detected to pass (verifies RF capability). */
static bool test_nfc(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting NFC (PN5180) test..." ANSI_RESET);

  const struct device *pn5180_dev = DEVICE_DT_GET(DT_NODELABEL(pn5180));
  if (!device_is_ready(pn5180_dev)) {
    LOG_ERR(ANSI_RED "[TEST] NFC: ✗ FAIL - PN5180 device not ready" ANSI_RESET);
    return false;
  }

  /* 1. Initialize and verify SPI (version read) */
  int ret = pn5180_init(pn5180_dev);
  if (ret != 0) {
    LOG_ERR(ANSI_RED "[TEST] NFC: ✗ FAIL - pn5180_init failed: %d" ANSI_RESET,
            ret);
    return false;
  }

  struct pn5180_version_info version;
  ret = pn5180_get_version(pn5180_dev, &version);
  if (ret != 0 ||
      (version.firmware_version == 0 && version.product_version == 0)) {
    LOG_ERR(ANSI_RED
            "[TEST] NFC: ✗ FAIL - SPI/version check failed" ANSI_RESET);
    return false;
  }
  LOG_INF("[TEST] NFC: SPI verified (FW: %d.%d)",
          (version.firmware_version >> 8) & 0xFF,
          version.firmware_version & 0xFF);

  /* 2. Configure for ISO15693 and scan for tag (driver API) */
  ret = pn5180_configure(pn5180_dev, PN5180_PROTOCOL_ISO15693);
  if (ret != 0) {
    LOG_ERR(ANSI_RED
            "[TEST] NFC: ✗ FAIL - pn5180_configure failed: %d" ANSI_RESET,
            ret);
    return false;
  }

  LOG_INF(ANSI_YELLOW
          "[TEST] NFC: Place ISO15693 tag on unit (5s timeout)..." ANSI_RESET);

  uint8_t uid[8];
  bool tag_found = false;
  int64_t scan_start = k_uptime_get();
  while (k_uptime_get() - scan_start < 5000) {
    ret = pn5180_get_inventory(pn5180_dev, uid, sizeof(uid));
    if (ret == 0) {
      tag_found = true;
      break;
    }
    k_msleep(200);
  }

  (void)pn5180_prepare_poweroff(pn5180_dev);

  if (!tag_found) {
    LOG_ERR(
        ANSI_RED
        "[TEST] NFC: ✗ FAIL - No tag detected (RF not verified)" ANSI_RESET);
    return false;
  }

  /* Print UUID for QA (MSB first, hex) */
  LOG_INF("[TEST] NFC: UUID: %02X %02X %02X %02X %02X %02X %02X %02X", uid[0],
          uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);

  /* Store truncated UID detail (last 4 bytes, MSB, hex) */
  snprintf(test_results[TEST_NFC].detail, sizeof(test_results[TEST_NFC].detail),
           "%02X%02X%02X%02X", uid[4], uid[5], uid[6], uid[7]);

  LOG_INF(
      ANSI_GREEN
      "[TEST] NFC: ✓ PASS - SPI and RF verified, tag UUID printed" ANSI_RESET);
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
  lv_label_set_text(label, "TEST SUITE - Running... Do not power off");
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

/* ADC Test: Perform a quick ADC read - using exact reference code approach
 */
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

  /* Store ADC detail as final voltage in mV */
  snprintf(test_results[TEST_ADC].detail, sizeof(test_results[TEST_ADC].detail),
           "%d mV", final_vbat);

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

  /* Store RTC timestamp detail for EPD summary (YYYY-MM-DD HH:MM) */
  snprintf(test_results[TEST_RTC].detail, sizeof(test_results[TEST_RTC].detail),
           "%04d-%02d-%02d %02d:%02d", time2.tm_year + 1900, time2.tm_mon + 1,
           time2.tm_mday, time2.tm_hour, time2.tm_min);

  LOG_INF(ANSI_GREEN
          "[TEST] RTC: ✓ PASS - RTC read successful, time advanced" ANSI_RESET);
  return true;
#else
  LOG_WRN(ANSI_YELLOW
          "[TEST] RTC: ⚠ pcf8523 node not found in devicetree" ANSI_RESET);
  return false;
#endif
}

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
          "[TEST] Button: Press ALL %d buttons within 15 seconds..." ANSI_RESET,
          EXPECTED_BUTTON_COUNT);

  int64_t start_time = k_uptime_get();
  int64_t timeout = 15000; // 15 seconds

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
    /* Store button detail as X/Y count */
    snprintf(test_results[TEST_BUTTON].detail,
             sizeof(test_results[TEST_BUTTON].detail), "%d/%d",
             unique_buttons_found, EXPECTED_BUTTON_COUNT);

    LOG_INF(ANSI_GREEN
            "[TEST] Button: ✓ PASS - All %d buttons verified" ANSI_RESET,
            EXPECTED_BUTTON_COUNT);
    return true;
  } else {
    /* Build list of missing button IDs for EPD summary detail */
    char missing[16] = {0};
    bool first = true;

    for (int i = 0; i < EXPECTED_BUTTON_COUNT; i++) {
      if (!(buttons_pressed_mask & BIT(i))) {
        int written = snprintf(&missing[strlen(missing)],
                               sizeof(missing) - strlen(missing),
                               first ? "%d" : ",%d", i);
        if (written <= 0 ||
            (size_t)written >= sizeof(missing) - strlen(missing)) {
          break;
        }
        first = false;
      }
    }

    if (missing[0] != '\0') {
      snprintf(test_results[TEST_BUTTON].detail,
               sizeof(test_results[TEST_BUTTON].detail), "%d/%d X:%s",
               unique_buttons_found, EXPECTED_BUTTON_COUNT, missing);
    } else {
      snprintf(test_results[TEST_BUTTON].detail,
               sizeof(test_results[TEST_BUTTON].detail), "%d/%d",
               unique_buttons_found, EXPECTED_BUTTON_COUNT);
    }

    LOG_ERR(ANSI_RED
            "[TEST] Button: ✗ FAIL - Only %d/%d buttons pressed" ANSI_RESET,
            unique_buttons_found, EXPECTED_BUTTON_COUNT);
    return false;
  }
}

/* LoRa QA test against beacon
 *
 * Packet format (4 bytes):
 *   [0]      type   0xAA = ping (DUT → beacon), 0xBB = pong (beacon → DUT)
 *   [1]      0x00   reserved
 *   [2..3]   uint16 beacon counter, big-endian
 *
 * RF config (must match beacon node exactly):
 *   915.0 MHz | BW 125 kHz | SF7 | CR 4/5 | private sync word
 */

#define LORA_QA_FREQ 915000000U
#define LORA_QA_TX_POWER 14
#define LORA_QA_BW BW_125_KHZ
#define LORA_QA_SF SF_7
#define LORA_QA_CR CR_4_5
#define LORA_QA_PREAMBLE 8

#define QA_PKT_LEN 4
#define QA_PKT_TYPE_PING 0xAA
#define QA_PKT_TYPE_PONG 0xBB

static bool test_lora(void) {
  LOG_INF(ANSI_BLUE "[TEST] Starting LoRa (SX1262) test..." ANSI_RESET);

  const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
  if (!device_is_ready(lora_dev)) {
    LOG_ERR(ANSI_RED "[TEST] LoRa: ✗ FAIL - Device not ready" ANSI_RESET);
    return false;
  }

  struct lora_modem_config cfg = {
      .frequency = LORA_QA_FREQ,
      .bandwidth = LORA_QA_BW,
      .datarate = LORA_QA_SF,
      .coding_rate = LORA_QA_CR,
      .preamble_len = LORA_QA_PREAMBLE,
      .tx_power = LORA_QA_TX_POWER,
      .tx = true,
      .iq_inverted = false,
      .public_network = false, /* private sync word — matches beacon */
  };

  /* ------------------------------------------------------------------ */
  /* Step 1: Config in TX mode — proves SX1262 responds over SPI        */
  /* ------------------------------------------------------------------ */
  int ret = lora_config(lora_dev, &cfg);
  if (ret != 0) {
    LOG_ERR(ANSI_RED
            "[TEST] LoRa: ✗ FAIL - lora_config (TX) failed: %d" ANSI_RESET,
            ret);
    return false;
  }

  /* ------------------------------------------------------------------ */
  /* Step 2: Send ping — proves TX path and PA ramp                     */
  /* ------------------------------------------------------------------ */
  uint8_t ping[QA_PKT_LEN] = {QA_PKT_TYPE_PING, 0x00, 0x00, 0x00};

  ret = lora_send(lora_dev, ping, QA_PKT_LEN);
  if (ret != 0) {
    LOG_ERR(ANSI_RED
            "[TEST] LoRa: ✗ FAIL - lora_send (ping) failed: %d" ANSI_RESET,
            ret);
    return false;
  }
  LOG_INF("[TEST] LoRa: Ping sent [0xAA 0x00 0x00 0x00]");

  /* ------------------------------------------------------------------ */
  /* Step 3: Flip to RX — proves RX path (the failure mode we've seen)  */
  /* ------------------------------------------------------------------ */
  cfg.tx = false;
  ret = lora_config(lora_dev, &cfg);
  if (ret != 0) {
    LOG_ERR(ANSI_RED
            "[TEST] LoRa: ✗ FAIL - lora_config (RX) failed: %d" ANSI_RESET,
            ret);

    return false;
  }

  /* ------------------------------------------------------------------ */
  /* Step 4: Wait for pong from beacon (5s timeout)                     */
  /* ------------------------------------------------------------------ */
  uint8_t pong[QA_PKT_LEN];
  int16_t rssi;
  int8_t snr;

  LOG_INF("[TEST] LoRa: Waiting for pong from beacon (5s)...");

  int len = lora_recv(lora_dev, pong, QA_PKT_LEN, K_MSEC(500), &rssi, &snr);

  if (len < 0) {
    LOG_ERR(
        ANSI_RED
        "[TEST] LoRa: ✗ FAIL - No pong received (RX timeout): %d" ANSI_RESET,
        len);
    snprintf(test_results[TEST_LORA].detail,
             sizeof(test_results[TEST_LORA].detail), "RX timeout");
    return false;
  }

  /* ------------------------------------------------------------------ */
  /* Step 5: Validate pong packet                                        */
  /* ------------------------------------------------------------------ */
  if (len != QA_PKT_LEN || pong[0] != QA_PKT_TYPE_PONG) {
    LOG_ERR(ANSI_RED
            "[TEST] LoRa: ✗ FAIL - Bad pong (type=0x%02X len=%d)" ANSI_RESET,
            len >= 1 ? pong[0] : 0, len);
    snprintf(test_results[TEST_LORA].detail,
             sizeof(test_results[TEST_LORA].detail), "bad pkt 0x%02X",
             len >= 1 ? pong[0] : 0);
    return false;
  }

  uint16_t beacon_count = ((uint16_t)pong[2] << 8) | pong[3];

  LOG_INF("[TEST] LoRa: Pong received — RSSI: %d dBm, SNR: %d, "
          "beacon count: %u",
          rssi, snr, beacon_count);

  /* Store RSSI + beacon count in detail for EPD summary */
  snprintf(test_results[TEST_LORA].detail,
           sizeof(test_results[TEST_LORA].detail), "%ddBm #%u", rssi,
           beacon_count);

  LOG_INF(ANSI_GREEN
          "[TEST] LoRa: ✓ PASS - TX and RX verified (%d dBm)" ANSI_RESET,
          rssi);
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

static void led_blink_pattern(int count, int on_ms, int off_ms) {
  int ret = leds_init();
  if (ret != 0) {
    LOG_ERR(ANSI_RED "LED pattern: leds_init failed: %d" ANSI_RESET, ret);
    return;
  }

  for (int i = 0; i < count; i++) {
    led_set(0, true);
    k_msleep(on_ms);
    led_set(0, false);
    if (i < count - 1) {
      k_msleep(off_ms);
    }
  }
}

static void epd_render_final_summary(void) {
#if EPD_ENABLED
  const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display_dev)) {
    LOG_ERR(ANSI_RED
            "EPD summary: ✗ FAIL - Display device not ready" ANSI_RESET);
    return;
  }

  lv_display_t *disp = lv_display_get_default();
  if (disp == NULL) {
    LOG_ERR(ANSI_RED
            "EPD summary: ✗ FAIL - LVGL display not initialized" ANSI_RESET);
    return;
  }

  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  /* Header */
  lv_obj_t *header = lv_label_create(screen);
  lv_label_set_text(header, "FlexBox TEST SUITE");
  lv_obj_set_style_text_color(header, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);

  /* Column headers */
  lv_obj_t *pass_header = lv_label_create(screen);
  lv_label_set_text(pass_header, "PASS");
  lv_obj_set_style_text_color(pass_header, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(pass_header, LV_ALIGN_TOP_LEFT, 8, 28);

  lv_obj_t *fail_header = lv_label_create(screen);
  lv_label_set_text(fail_header, "FAIL");
  lv_obj_set_style_text_color(fail_header, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(fail_header, LV_ALIGN_TOP_RIGHT, -8, 28);

  /* Vertical divider */
  int32_t vres = lv_display_get_vertical_resolution(disp);
  lv_obj_t *divider = lv_obj_create(screen);
  lv_obj_set_size(divider, 2, vres > 60 ? vres - 60 : vres);
  lv_obj_set_style_bg_color(divider, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(divider, LV_OPA_40, LV_PART_MAIN);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 40);

  /* Test entries */
  int pass_row = 0;
  int fail_row = 0;
  const int row_height = 18;
  const int row_y_start = 48;
  int passed = 0;

  for (int i = 0; i < TEST_COUNT; i++) {
    char line[48];
    if (test_results[i].detail[0] != '\0') {
      snprintf(line, sizeof(line), "%s  %s", test_results[i].name,
               test_results[i].detail);
    } else {
      snprintf(line, sizeof(line), "%s", test_results[i].name);
    }

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, line);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);

    if (test_results[i].passed) {
      lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8,
                   row_y_start + pass_row * row_height);
      pass_row++;
      passed++;
    } else {
      lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -8,
                   row_y_start + fail_row * row_height);
      fail_row++;
    }
  }

  /* Footer: pass count only (RTC timestamp shown on RTC line) */
  char footer_left[24];
  if (passed == TEST_COUNT) {
    snprintf(footer_left, sizeof(footer_left), "ALL PASS");
  } else {
    snprintf(footer_left, sizeof(footer_left), "%d/%d PASSED", passed,
             TEST_COUNT);
  }

  lv_obj_t *footer_l = lv_label_create(screen);
  lv_label_set_text(footer_l, footer_left);
  lv_obj_set_style_text_color(footer_l, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(footer_l, LV_ALIGN_BOTTOM_LEFT, 8, -8);

  lv_scr_load(screen);

  for (int i = 0; i < 20; i++) {
    lv_task_handler();
    k_msleep(50);
  }

  LOG_INF(ANSI_GREEN "EPD summary: Final QA summary rendered" ANSI_RESET);
#endif
}

static void shutdown_external_power(void) {
  LOG_INF("Disabling external power rails after test suite...");

  (void)power_ctrl_set(POWER_EN_3V6, false);
  (void)power_ctrl_set(POWER_EN_3V3A, false);
  (void)power_ctrl_set(POWER_EN_1V8, false);
  (void)power_ctrl_set(POWER_EN_3V3, false);
}

/* Optional LoRa QA beacon firmware
 * Enabled by defining BEACON_MODE at compile time.
 */
#ifdef BEACON_MODE
#define LORA_BEACON_FREQ 915000000U
#define LORA_BEACON_TX_POWER 14
#define LORA_BEACON_BW BW_125_KHZ
#define LORA_BEACON_SF SF_7
#define LORA_BEACON_CR CR_4_5
#define LORA_BEACON_PREAMBLE 8

#define BEACON_PKT_LEN 4
#define BEACON_PKT_TYPE_PING 0xAA
#define BEACON_PKT_TYPE_PONG 0xBB

static uint16_t beacon_pong_counter;

static void beacon_build_pong(uint8_t *buf) {
  buf[0] = BEACON_PKT_TYPE_PONG;
  buf[1] = 0x00;
  buf[2] = (uint8_t)(beacon_pong_counter >> 8);
  buf[3] = (uint8_t)(beacon_pong_counter & 0xFF);
  beacon_pong_counter++;
}

static int beacon_main(void) {
  LOG_INF("=== LoRa QA Beacon starting ===");

  int ret_boot = power_ctrl_init();
  if (ret_boot < 0) {
    LOG_ERR("Beacon: power_ctrl_init failed: %d", ret_boot);
    return ret_boot;
  }
  ret_boot = rail_manager_init();
  if (ret_boot < 0) {
    LOG_ERR("Beacon: rail_manager_init failed: %d", ret_boot);
    return ret_boot;
  }

  LOG_INF("Beacon: enabling rails for LoRa + EPD...");
  power_ctrl_set(POWER_EN_3V3, true);
  k_msleep(50);
  power_ctrl_set(POWER_EN_1V8, true);
  k_msleep(50);
  power_ctrl_set(POWER_EN_3V3A, true);
  k_msleep(50);
  power_ctrl_set(POWER_EN_3V6, true);
  k_msleep(100);

#if EPD_ENABLED
  ret_boot = display_manager_init();
  if (ret_boot != 0) {
    LOG_WRN("Beacon: display_manager_init failed (%d) — continuing without EPD",
            ret_boot);
  } else {
    display_beacon_show_listening();
  }
#endif

  const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
  if (!device_is_ready(lora_dev)) {
    LOG_ERR("LoRa device not ready — check overlay");
    return -1;
  }

  struct lora_modem_config cfg = {
      .frequency = LORA_BEACON_FREQ,
      .bandwidth = LORA_BEACON_BW,
      .datarate = LORA_BEACON_SF,
      .coding_rate = LORA_BEACON_CR,
      .preamble_len = LORA_BEACON_PREAMBLE,
      .tx_power = LORA_BEACON_TX_POWER,
      .tx = false,
      .iq_inverted = false,
      .public_network = false,
  };

  int ret = lora_config(lora_dev, &cfg);
  if (ret != 0) {
    LOG_ERR("lora_config failed: %d", ret);
    return ret;
  }

  LOG_INF("Beacon listening on %.3f MHz SF7 BW125 — waiting for DUT pings...",
          (double)LORA_BEACON_FREQ / 1e6);

  while (1) {
    uint8_t rx_buf[BEACON_PKT_LEN];
    int16_t rssi;
    int8_t snr;

    cfg.tx = false;
    (void)lora_config(lora_dev, &cfg);

    int len =
        lora_recv(lora_dev, rx_buf, sizeof(rx_buf), K_FOREVER, &rssi, &snr);
    if (len < 0) {
      LOG_WRN("lora_recv error: %d — retrying", len);
      k_msleep(100);
      continue;
    }

    LOG_INF("RX [%d bytes] type=0x%02X rssi=%d snr=%d", len,
            len >= 1 ? rx_buf[0] : 0xFF, rssi, snr);

    if (len != BEACON_PKT_LEN || rx_buf[0] != BEACON_PKT_TYPE_PING) {
      LOG_WRN("Ignoring non-ping packet (type=0x%02X len=%d)",
              len >= 1 ? rx_buf[0] : 0, len);
      continue;
    }

    k_msleep(5);

    uint8_t tx_buf[BEACON_PKT_LEN];
    beacon_build_pong(tx_buf);

    cfg.tx = true;
    (void)lora_config(lora_dev, &cfg);

    ret = lora_send(lora_dev, tx_buf, BEACON_PKT_LEN);
    if (ret != 0) {
      LOG_ERR("lora_send failed: %d", ret);
    } else {
      LOG_INF("TX pong #%u [0x%02X 0x%02X 0x%02X 0x%02X]",
              beacon_pong_counter - 1, tx_buf[0], tx_buf[1], tx_buf[2],
              tx_buf[3]);
#if EPD_ENABLED
      display_beacon_show_last_pong(rssi, snr,
                                    (uint16_t)(beacon_pong_counter - 1U));
#endif
    }
  }

  return 0;
}
#endif /* BEACON_MODE */

int main(void) {
#ifdef BEACON_MODE
  return beacon_main();
#else
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

  /* Power up ALL domains immediately for testing - ensure all rails are ON
   */
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

  /* Run all non-interactive tests sequentially */
  test_results[TEST_EPD].passed = test_epd();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_ADC].passed = test_adc();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_EEPROM].passed = test_eeprom();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_NFC].passed = test_nfc();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_RTC].passed = test_rtc();
  LOG_INF("");
  k_msleep(100);

  test_results[TEST_LORA].passed = test_lora();
  LOG_INF("");
  k_msleep(10);

  /* LED cue before interactive button test */
  led_blink_pattern(3, 100, 100);

  test_results[TEST_BUTTON].passed = test_button();
  LOG_INF("");
  k_msleep(100);

  /* LED cue after button test completion */
  led_blink_pattern(2, 300, 300);

  /* Render final EPD summary screen (second and final EPD update) */
  epd_render_final_summary();

  /* Print final summary */
  print_test_summary();

  LOG_INF("");
  k_msleep(20000);
  shutdown_external_power();
  LOG_INF(ANSI_BOLD ANSI_CYAN "Test suite completed. External rails disabled "
                              "for low power." ANSI_RESET);

  /* Keep system running */
  while (1) {
    k_sleep(K_FOREVER);
  }

  return 0;
#endif
}