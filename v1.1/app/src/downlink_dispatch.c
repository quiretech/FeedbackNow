#include "downlink_dispatch.h"
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

void downlink_queue_housekeeping_state_snapshot(void) {
  if (!lora_is_joined()) {
    return;
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
    return;
  }

  lora_uplink_msg_t m = {0};
  m.port = FPORT_HOUSEKEEPING;
  m.confirmed = LORA_HEARTBEAT_UPLINK_CONFIRMED;
  m.len = PAYLOAD_LEN_BYTES;
  memcpy(m.data, snap, PAYLOAD_LEN_BYTES);

  if (lora_put_event(&m, K_MSEC(500)) != 0) {
    LOG_WRN("[downlink] HK state snapshot: queue failed (0x13 fport %u)",
            (unsigned)FPORT_HOUSEKEEPING);
  }
}

static void downlink_queue_fw_hw_version_uplink(void) {
  if (!lora_is_joined()) {
    LOG_WRN("[downlink] fw/hw version query (0x08): not joined, skip uplink");
    return;
  }

  uint32_t epoch_s = 0;
  if (rtc_get_epoch_seconds(&epoch_s) != 0 || epoch_s == 0) {
    epoch_s = (uint32_t)(k_uptime_get() / 1000U);
  }

  uint8_t ver[PAYLOAD_LEN_BYTES];
  if (payload_gen_build_device_version_info(epoch_s, ver) != 0) {
    return;
  }

  lora_uplink_msg_t m = {0};
  m.port = FPORT_DEVICE_INFO;
  m.confirmed = LORA_HEARTBEAT_UPLINK_CONFIRMED;
  m.len = PAYLOAD_LEN_BYTES;
  memcpy(m.data, ver, PAYLOAD_LEN_BYTES);

  if (lora_put_event(&m, K_MSEC(500)) != 0) {
    LOG_WRN("[downlink] fw/hw version query (0x08): queue EVT 0x14 failed");
  } else {
    LOG_INF("[downlink] fw/hw version query (0x08): queued EVT 0x14 fport %u",
            (unsigned)FPORT_DEVICE_INFO);
  }
}

void downlink_dispatch(uint8_t port, uint8_t len, const uint8_t *frmpayload,
                       const struct downlink_dispatch_ops *ops) {
  if (frmpayload == NULL || len == 0 || ops == NULL) {
    return;
  }

  uint8_t cmd = frmpayload[0];
  LOG_INF("[downlink] dispatch port=%u len=%u cmd=0x%02X", (unsigned)port,
          (unsigned)len, cmd);

  switch (cmd) {
  case DL_CMD_EPD_UPDATE:
    if (len >= 5) {
      uint32_t epoch =
          ((uint32_t)frmpayload[1] << 24) | ((uint32_t)frmpayload[2] << 16) |
          ((uint32_t)frmpayload[3] << 8) | (uint32_t)frmpayload[4];
      display_set_pending_last_cleaned_and_apply(epoch);
      LOG_INF("[downlink] cmd 0x%02X EPD update epoch=%u", (unsigned)cmd,
              (unsigned)epoch);
    } else {
      LOG_WRN("[downlink] cmd 0x%02X EPD update: len %u < 5", (unsigned)cmd,
              (unsigned)len);
    }
    break;
  case DL_CMD_EPD_REFRESH:
    display_request_full_refresh();
    LOG_INF("[downlink] cmd 0x%02X EPD refresh", (unsigned)cmd);
    break;
  case DL_CMD_STATUS_REQ:
    LOG_INF("[downlink] cmd 0x%02X status request (heartbeat / telemetry)",
            (unsigned)cmd);
    housekeeping_run();
    break;
  case DL_CMD_RESET_COUNTERS:
    LOG_INF("[downlink] cmd 0x%02X reset counters", (unsigned)cmd);
    rail_manager_request_3v3a();
    if (button_counter_store_factory_reset() == 0) {
      LOG_INF("[downlink] counters reset done");
    } else {
      LOG_ERR("[downlink] counters reset failed");
    }
    rail_manager_release_3v3a();
    break;
  case DL_CMD_FACTORY_RESET:
    LOG_INF("[downlink] cmd 0x%02X factory reset (counters + devnonce + "
            "has_joined_once)",
            (unsigned)cmd);
    rail_manager_request_3v3a();
    if (button_counter_store_factory_reset() == 0) {
      LOG_INF("[downlink] factory reset: counters done");
    } else {
      LOG_ERR("[downlink] factory reset: counters failed");
    }
    if (devnonce_store_factory_reset() == 0) {
      LOG_INF("[downlink] factory reset: devnonce reset to 0");
    } else {
      LOG_WRN("[downlink] factory reset: devnonce reset failed");
    }
    if (join_state_store_clear_has_joined_once() == 0) {
      LOG_INF("[downlink] factory reset: has_joined_once cleared");
    } else {
      LOG_WRN("[downlink] factory reset: clear has_joined_once failed");
    }
    if (ops->schedule_reboot_led_ms != NULL) {
      ops->schedule_reboot_led_ms(REBOOT_LED_MS);
    }
    rail_manager_release_3v3a();
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
        LOG_INF("[downlink] cmd 0x%02X timezone offset %d min", (unsigned)cmd,
                (int)offset_min);
        display_show_last_cleaned();
        downlink_queue_housekeeping_state_snapshot();
      } else {
        LOG_WRN("[downlink] cmd 0x%02X timezone store failed: %d",
                (unsigned)cmd, ret);
      }
    } else {
      LOG_WRN("[downlink] cmd 0x%02X timezone: len %u < 3", (unsigned)cmd,
              (unsigned)len);
    }
    break;
  case DL_CMD_REBOOT:
    LOG_INF("[downlink] cmd 0x%02X reboot (post SMF)", (unsigned)cmd);
    if (smf_post_event(SMF_EVT_DL_REBOOT, 0, k_uptime_get()) != 0) {
      LOG_WRN("[downlink] DL reboot: SMF queue post failed");
    }
    break;
  case DL_CMD_QUERY_FW_HW_VERSION:
    LOG_INF("[downlink] cmd 0x%02X fw/hw version query → EVT 0x14 fport %u",
            (unsigned)cmd, (unsigned)FPORT_DEVICE_INFO);
    downlink_queue_fw_hw_version_uplink();
    break;
  default:
    LOG_WRN("[downlink] unknown cmd 0x%02X", cmd);
    break;
  }
}
