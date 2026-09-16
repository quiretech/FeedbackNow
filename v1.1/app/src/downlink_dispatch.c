#include "downlink_dispatch.h"
#include "log_fmt.h"
#include "button_counter_store.h"
#include "devnonce_store.h"
#include "display_manager.h"
#include "housekeeping.h"
#include "join_state_store.h"
#include "last_cleaned_store.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "rail_manager.h"
#include "rtc.h"
#include "smf_system_mode.h"
#include "sys_config.h"
#include "tz_offset_store.h"

#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(downlink_dispatch, CONFIG_LOG_DEFAULT_LEVEL);

/* FRD 4.5 application downlink command bytes (first FRMPayload byte). */
#define DL_CMD_EPD_UPDATE        0x01
#define DL_CMD_EPD_REFRESH       0x02
#define DL_CMD_TIMEZONE_OFFSET   0x03
#define DL_CMD_STATUS_REQ        0x04
#define DL_CMD_RESET_COUNTERS    0x05
#define DL_CMD_FACTORY_RESET      0x06
#define DL_CMD_REBOOT             0x07
#define DL_CMD_QUERY_FW_HW_VERSION 0x08
/** Fullscreen EPD banner: [0x99][dur_min][ASCII hex pairs → message]. */
#define DL_CMD_CUSTOM_TEXT 0x99

static int dl_hex_nibble(uint8_t c) {
  if (c >= '0' && c <= '9') {
    return (int)(c - '0');
  }

  if (c >= 'A' && c <= 'F') {
    return 10 + (int)(c - 'A');
  }

  if (c >= 'a' && c <= 'f') {
    return 10 + (int)(c - 'a');
  }

  return -1;
}

/** ASCII hex digit pairs → bytes; printable ASCII 0x20–0x7E only (no NUL). */

static int dl_decode_hex_ascii_body(const uint8_t *hex, size_t hexlen, char *out,
                                    size_t outsz) {

  if (outsz == 0) {

    return -1;

  }

  if (hexlen % 2U != 0U) {

    return -1;

  }

  size_t o = 0;

  for (size_t i = 0; i < hexlen; i += 2U) {

    int hi = dl_hex_nibble(hex[i]);

    int lo = dl_hex_nibble(hex[i + 1]);

    if (hi < 0 || lo < 0) {

      return -1;

    }

    uint8_t b = (uint8_t)((hi << 4) | lo);

    if (b == 0U || b < 0x20U || b > 0x7EU) {

      return -1;

    }

    if (o + 1U >= outsz) {

      return -1;

    }

    out[o++] = (char)b;

  }

  out[o] = '\0';

  return 0;

}

bool downlink_queue_housekeeping_state_snapshot(void) {
  if (!lora_is_joined()) {
    return false;
  }

  uint32_t epoch_s = 0;
  if (rtc_get_epoch_seconds(&epoch_s) != 0 || epoch_s == 0) {
    epoch_s = (uint32_t)(k_uptime_get() / 1000U);
  }

  uint32_t last_cleaned = 0;
  if (!last_cleaned_store_get(&last_cleaned)) {
    last_cleaned = 0;
  }

  int16_t tz_min = 0;
  if (tz_offset_store_get(&tz_min) != 0) {
    tz_min = 0;
  }

  uint8_t snap[PAYLOAD_LEN_BYTES];
  if (payload_gen_build_device_state_snapshot(epoch_s, last_cleaned, tz_min,
                                              snap) != 0) {
    return false;
  }

  lora_uplink_msg_t m = {0};
  m.port = FPORT_HOUSEKEEPING;
  m.confirmed = LORA_HEARTBEAT_UPLINK_CONFIRMED;
  m.len = PAYLOAD_LEN_BYTES;
  memcpy(m.data, snap, PAYLOAD_LEN_BYTES);

  if (lora_put_event(&m, K_MSEC(500)) != 0) {
    LOG_WRN("HK state snapshot queue failed (0x13 fport %u)",
            (unsigned)FPORT_HOUSEKEEPING);
    return false;
  }
  return true;
}

void factory_reset_perform(const struct downlink_dispatch_ops *ops,
                           bool reset_counters) {
  LOG_WRN("factory reset: devnonce + has_joined_once%s",
          reset_counters ? " + counters" : " (counters kept)");
  rail_manager_request_3v3a();
  if (reset_counters) {
    if (button_counter_store_factory_reset() == 0) {
      LOG_DBG("factory reset: counters done");
    } else {
      LOG_ERR("factory reset: counters failed");
    }
  }
  if (devnonce_store_factory_reset() == 0) {
    LOG_DBG("factory reset: devnonce reset to 0");
  } else {
    LOG_WRN("factory reset: devnonce reset failed");
  }
  if (join_state_store_clear_has_joined_once() == 0) {
    LOG_DBG("factory reset: has_joined_once cleared");
  } else {
    LOG_WRN("factory reset: clear has_joined_once failed");
  }
  if (ops != NULL && ops->schedule_reboot_led_ms != NULL) {
    ops->schedule_reboot_led_ms(REBOOT_LED_MS);
  }
  rail_manager_release_3v3a();
}

static void downlink_queue_fw_hw_version_uplink(void) {
  if (!lora_is_joined()) {
    LOG_WRN("fw/hw query (0x08): not joined, skip uplink");
    return;
  }

  uint8_t ver[PAYLOAD_LEN_BYTES];
  if (payload_gen_build_device_version_info(ver) != 0) {
    return;
  }

  lora_uplink_msg_t m = {0};
  m.port = FPORT_DEVICE_INFO;
  m.confirmed = LORA_HEARTBEAT_UPLINK_CONFIRMED;
  m.len = PAYLOAD_LEN_BYTES;
  memcpy(m.data, ver, PAYLOAD_LEN_BYTES);

  if (lora_put_event(&m, K_MSEC(500)) != 0) {
    LOG_WRN("fw/hw query (0x08): queue EVT 0x14 failed");
  } else {
    LOG_DBG("fw/hw query (0x08): queued EVT 0x14 fport %u",
            (unsigned)FPORT_DEVICE_INFO);
  }
}

void downlink_dispatch(uint8_t port, uint8_t len, const uint8_t *frmpayload,
                       const struct downlink_dispatch_ops *ops) {
  if (frmpayload == NULL || len == 0 || ops == NULL) {
    return;
  }

  uint8_t cmd = frmpayload[0];

  LOG_EVT("DL port=%u len=%u cmd=0x%02X", (unsigned)port,
          (unsigned)len, cmd);

  switch (cmd) {
  case DL_CMD_EPD_UPDATE:
    if (len >= 5) {
      uint32_t epoch =
          ((uint32_t)frmpayload[1] << 24) | ((uint32_t)frmpayload[2] << 16) |
          ((uint32_t)frmpayload[3] << 8) | (uint32_t)frmpayload[4];
      if (last_cleaned_store_set(epoch) != 0) {
        LOG_WRN("cmd 0x%02X last_cleaned EEPROM write failed epoch=%u",
                (unsigned)cmd, (unsigned)epoch);
      }
      display_set_pending_last_cleaned_and_apply(epoch);
      LOG_DBG("cmd 0x%02X EPD update epoch=%u (EEPROM + display)",
              (unsigned)cmd, (unsigned)epoch);
    } else {
      LOG_WRN("cmd 0x%02X EPD update: len %u < 5", (unsigned)cmd,
              (unsigned)len);
    }
    break;
  case DL_CMD_EPD_REFRESH:
    display_request_full_refresh();
    LOG_DBG("cmd 0x%02X EPD refresh", (unsigned)cmd);
    break;
  case DL_CMD_STATUS_REQ:
    LOG_DBG("cmd 0x%02X status request (HK + counter sync burst)",
            (unsigned)cmd);
    housekeeping_submit_status_request();
    break;
  case DL_CMD_RESET_COUNTERS:
    LOG_DBG("cmd 0x%02X reset counters", (unsigned)cmd);
    rail_manager_request_3v3a();
    if (button_counter_store_factory_reset() == 0) {
      LOG_DBG("counters reset done");
    } else {
      LOG_ERR("counters reset failed");
    }
    rail_manager_release_3v3a();
    break;
  case DL_CMD_FACTORY_RESET:
    LOG_DBG("cmd 0x%02X factory reset", (unsigned)cmd);
    factory_reset_perform(ops, true);
    break;
  case DL_CMD_TIMEZONE_OFFSET:
    if (len >= 3) {
      int16_t offset_min =
          (int16_t)((uint16_t)frmpayload[1] << 8 | (uint16_t)frmpayload[2]);
      if (offset_min < TZ_OFFSET_MIN_MINUTES) {
        offset_min = TZ_OFFSET_MIN_MINUTES;
      }
      if (offset_min > TZ_OFFSET_MAX_MINUTES) {
        offset_min = TZ_OFFSET_MAX_MINUTES;
      }
      rail_manager_request_3v3a();
      int ret = tz_offset_store_set(offset_min);
      rail_manager_release_3v3a();
      if (ret == 0) {
        LOG_DBG("cmd 0x%02X timezone offset %d min", (unsigned)cmd,
                (int)offset_min);
#if DEVICE_HW_VARIANT != FLEXBOX_PLUS_MED
        display_show_last_cleaned(); // This is to ensure that the display reflects the new timezone offset immediately after it is set.
#endif /* FLEXBOX_PLUS_MED */
        downlink_queue_housekeeping_state_snapshot();
      } else {
        LOG_WRN("cmd 0x%02X timezone store failed: %d",
                (unsigned)cmd, ret);
      }
    } else {
      LOG_WRN("cmd 0x%02X timezone: len %u < 3", (unsigned)cmd,
              (unsigned)len);
    }
    break;
  case DL_CMD_REBOOT:
    LOG_DBG("cmd 0x%02X reboot (post SMF)", (unsigned)cmd);
    if (smf_post_event(SMF_EVT_DL_REBOOT, 0, k_uptime_get()) != 0) {
      LOG_WRN("DL reboot: SMF queue post failed");
    }
    break;
  case DL_CMD_QUERY_FW_HW_VERSION:
    LOG_DBG("cmd 0x%02X fw/hw version query -> EVT 0x14 fport %u",
            (unsigned)cmd, (unsigned)FPORT_DEVICE_INFO);
    downlink_queue_fw_hw_version_uplink();
    break;
  case DL_CMD_CUSTOM_TEXT:
    if (len >= 2U) {

      uint8_t dur_min = frmpayload[1];

      const uint8_t *hex = frmpayload + 2;

      size_t hexlen = (size_t)len - 2U;

      char decoded[DISPLAY_DL_CUSTOM_TEXT_MAX + 1];

      LOG_DBG("cmd 0x99 custom EPD text dur=%umin hex_len=%zu",

              (unsigned)dur_min, hexlen);

      if (dl_decode_hex_ascii_body(hex, hexlen, decoded, sizeof(decoded)) != 0) {

        LOG_WRN("0x99: invalid hex (pairs, printable ASCII)");

      } else {

#if EPD_ENABLED

        display_show_dl_custom_message(decoded, (uint32_t)dur_min);

#else

        LOG_DBG("0x99: EPD disabled, skip");

#endif

      }

    } else {

      LOG_WRN("cmd 0x99: len %u < 2", (unsigned)len);

    }

    break;
  default:
    LOG_WRN("unknown cmd 0x%02X", cmd);
    break;
  }
}
