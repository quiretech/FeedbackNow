/**
 * System mode FSM: single thread, single input queue (per architecture).
 * States: Normal, Staff, NFCScan, DeviceInfo, Reboot, ProcessAction.
 * Implements: timeouts (Staff 10s, DeviceInfo 30s), Staff-first for
 * Join/Reboot.
 *
 * Power gating: SMF owns 3.3A (peripheral rail) for all flows it dispatches.
 * request_3v3a() before LED/EEPROM/RTC work, release_3v3a() when leaving the
 * activity or mode. LoRa thread and async EEPROM flush keep their own
 * request/release where they are not driven by SMF.
 */
#include "smf_system_mode.h"
#include "app_logic.h"
#include "battery_adc.h"
#include "button_counter_store.h"
#include "devnonce_store.h"
#include "display_manager.h"
#include "join_state_store.h"
#include "last_cleaned_store.h"
#include "led_manager.h"
#include "lora_app.h"
#include "nfc_service.h"
#include "payload_gen.h"
#include "rail_manager.h"
#include "rtc.h"
#include "sys_config.h"
#include "tz_offset_store.h"

#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(smf, CONFIG_LOG_DEFAULT_LEVEL);

enum system_mode {
  MODE_NORMAL,
  MODE_STAFF,
  MODE_NFC_SCAN,
  MODE_DEVICE_INFO,
  MODE_REBOOT,
  MODE_PROCESS_ACTION,
  MODE_COUNT
};

#define SMF_MSGQ_SIZE 16
#define SMF_MSGQ_ALIGN 4

K_MSGQ_DEFINE(smf_msgq, sizeof(smf_msg_t), SMF_MSGQ_SIZE, SMF_MSGQ_ALIGN);

/* Downlink payload copy (written by smf_post_downlink, read by SMF handler) */
static uint8_t smf_dl_payload[LORA_MAX_PAYLOAD_SIZE];
static struct k_mutex smf_dl_mutex;

/* NFC result: 4-byte card data (written by smf_post_nfc_result, read by SMF) */
#define SMF_NFC_DATA_SIZE 4
static uint8_t smf_nfc_data[SMF_NFC_DATA_SIZE];
static struct k_mutex smf_nfc_mutex;

#define SMF_THREAD_STACK_SIZE 1536
#define SMF_THREAD_PRIORITY 6

/* Downlink command codes (FRD 4.5) */
#define DL_CMD_EPD_UPDATE 0x01
#define DL_CMD_EPD_REFRESH 0x02
#define DL_CMD_TIMEZONE_OFFSET 0x03

#define DL_CMD_STATUS_REQ 0x04
#define DL_CMD_RESET_COUNTERS 0x05
#define DL_CMD_FACTORY_RESET 0x06

/* Counter-sync: small delay between uplinks to avoid congestion */
#define COUNTER_SYNC_DELAY_MS 3000

/* Mode timeout: timer posts this event so SMF returns to Normal */
static volatile uint8_t mode_timeout_ev;

/* Set when SMF requested time sync from housekeeping; release 3.3A on
 * TIME_SYNC_DONE */
static bool housekeeping_holding_3v3a;

static void mode_timeout_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);
  if (mode_timeout_ev != SMF_EVT_NONE) {
    (void)smf_post_event(mode_timeout_ev, 0, k_uptime_get());
    mode_timeout_ev = SMF_EVT_NONE;
  }
}

static void reboot_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);
  LOG_INF("[SMF] rebooting now");
  sys_reboot(SYS_REBOOT_COLD);
}

K_TIMER_DEFINE(mode_timeout_timer, mode_timeout_expiry, NULL);
K_TIMER_DEFINE(reboot_timer, reboot_expiry, NULL);

static const char *smf_ev_type_str(uint8_t ev_type) {
  switch (ev_type) {
  case SMF_EVT_NONE:
    return "NONE";
  case SMF_EVT_BUTTON_SINGLE_0:
    return "BUTTON_SINGLE_0";
  case SMF_EVT_BUTTON_SINGLE_1:
    return "BUTTON_SINGLE_1";
  case SMF_EVT_BUTTON_SINGLE_2:
    return "BUTTON_SINGLE_2";
  case SMF_EVT_BUTTON_SINGLE_3:
    return "BUTTON_SINGLE_3";
  case SMF_EVT_BUTTON_SINGLE_4:
    return "BUTTON_SINGLE_4";
  case SMF_EVT_BUTTON_SINGLE_5:
    return "BUTTON_SINGLE_5";
  case SMF_EVT_COMBO_STAFF:
    return "COMBO_STAFF";
  case SMF_EVT_COMBO_DEVICE_INFO:
    return "COMBO_DEVICE_INFO";
  case SMF_EVT_COMBO_JOIN:
    return "COMBO_JOIN";
  case SMF_EVT_COMBO_REBOOT:
    return "COMBO_REBOOT";
  case SMF_EVT_STAFF_TIMEOUT:
    return "STAFF_TIMEOUT";
  case SMF_EVT_NFC_TIMEOUT:
    return "NFC_TIMEOUT";
  case SMF_EVT_DEVICE_INFO_TIMEOUT:
    return "DEVICE_INFO_TIMEOUT";
  case SMF_EVT_JOINED:
    return "JOINED";
  case SMF_EVT_JOIN_STARTED:
    return "JOIN_STARTED";
  case SMF_EVT_JOIN_CYCLE_FAILED:
    return "JOIN_CYCLE_FAILED";
  case SMF_EVT_TIME_SYNC_DONE:
    return "TIME_SYNC_DONE";
  case SMF_EVT_DISCONNECTED:
    return "DISCONNECTED";
  case SMF_EVT_DOWNLINK:
    return "DOWNLINK";
  case SMF_EVT_NFC_RESULT:
    return "NFC_RESULT";
  case SMF_EVT_HOUSEKEEPING_TICK:
    return "HOUSEKEEPING_TICK";
  case SMF_EVT_SYSTEM_READY:
    return "SYSTEM_READY";
  default:
    return "?";
  }
}

static const char *smf_mode_str(enum system_mode mode) {
  switch (mode) {
  case MODE_NORMAL:
    return "Normal";
  case MODE_STAFF:
    return "Staff";
  case MODE_NFC_SCAN:
    return "NFCScan";
  case MODE_DEVICE_INFO:
    return "DeviceInfo";
  case MODE_REBOOT:
    return "Reboot";
  case MODE_PROCESS_ACTION:
    return "ProcessAction";
  default:
    return "?";
  }
}

/**
 * Queue counter-sync payloads (Event 0x12) for all buttons. Used on rejoin
 * (confirmed) and as part of heartbeat / on-demand status (unconfirmed).
 */
static void smf_do_counter_sync(bool confirmed) {
  uint32_t epoch_s = 0;

  (void)rtc_get_epoch_seconds(&epoch_s);
  if (epoch_s == 0) {
    epoch_s = (uint32_t)(k_uptime_get() / 1000U);
  }

  LOG_DBG("[SMF] counter_sync: Event 0x12 per button (confirmed=%d)",
          (int)confirmed);

  for (uint8_t btn = 0; btn < NUM_BUTTONS; btn++) {
    uint8_t payload[PAYLOAD_LEN_BYTES];
    int ret = payload_gen_build_counter_sync(btn, epoch_s, payload);
    if (ret != 0) {
      LOG_WRN("[SMF] counter_sync btn=%u build failed: %d", btn, ret);
      continue;
    }
    lora_uplink_msg_t msg = {0};
    msg.port = FPORT_HOUSEKEEPING;
    msg.confirmed = confirmed;
    msg.len = PAYLOAD_LEN_BYTES;
    memcpy(msg.data, payload, PAYLOAD_LEN_BYTES);
    ret = lora_put_event(&msg, K_MSEC(500));
    if (ret == 0) {
      LOG_INF("[SMF] counter_sync: queued button %u", btn);
    } else {
      LOG_WRN("[SMF] counter_sync: lora_put_event btn=%u failed: %d", btn, ret);
    }
    k_msleep(COUNTER_SYNC_DELAY_MS);
  }

  LOG_DBG("[SMF] counter_sync done");
}

static void smf_handle_downlink(uint8_t port, uint8_t len,
                                const uint8_t *data) {
  if (len == 0 || data == NULL) {
    return;
  }
  uint8_t cmd = data[0];
  LOG_INF("[SMF] DOWNLINK -> dispatch: port=%u len=%u cmd=0x%02X", port, len,
          cmd);

  switch (cmd) {
  case DL_CMD_EPD_UPDATE:
    if (len >= 5) {
      uint32_t epoch = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
                       ((uint32_t)data[3] << 8) | (uint32_t)data[4];
      display_set_pending_last_cleaned_and_apply(epoch);
      LOG_INF("[SMF] cmd 0x01 EPD update epoch=%u", (unsigned)epoch);
    } else {
      LOG_WRN("[SMF] cmd 0x01 EPD update: len %u < 5", len);
    }
    break;
  case DL_CMD_EPD_REFRESH:
    display_request_full_refresh();
    LOG_INF("[SMF] cmd 0x02 EPD refresh");
    break;
  case DL_CMD_STATUS_REQ:
    LOG_INF("[SMF] cmd 0x03 status request (trigger heartbeat on demand)");
    (void)smf_post_event(SMF_EVT_HOUSEKEEPING_TICK, 0, k_uptime_get());
    break;
  case DL_CMD_RESET_COUNTERS:
    LOG_INF("[SMF] cmd 0x04 reset counters");
    rail_manager_request_3v3a();
    if (button_counter_store_factory_reset() == 0) {
      LOG_INF("[SMF] counters reset done");
    } else {
      LOG_ERR("[SMF] counters reset failed");
    }
    rail_manager_release_3v3a();
    break;
  case DL_CMD_FACTORY_RESET:
    LOG_INF(
        "[SMF] cmd 0x06 factory reset (counters + devnonce + has_joined_once)");
    rail_manager_request_3v3a();
    if (button_counter_store_factory_reset() == 0) {
      LOG_INF("[SMF] factory reset: counters done");
    } else {
      LOG_ERR("[SMF] factory reset: counters failed");
    }
    if (devnonce_store_factory_reset() == 0) {
      LOG_INF("[SMF] factory reset: devnonce reset to 0");
    } else {
      LOG_WRN("[SMF] factory reset: devnonce reset failed");
    }
    if (join_state_store_clear_has_joined_once() == 0) {
      LOG_INF("[SMF] factory reset: has_joined_once cleared");
    } else {
      LOG_WRN("[SMF] factory reset: clear has_joined_once failed");
    }
    (void)led_manager_show(0, LED_PATTERN_POWER_ON);
    k_timer_start(&reboot_timer, K_MSEC(REBOOT_LED_MS), K_NO_WAIT);
    rail_manager_release_3v3a();
    break;
  case DL_CMD_TIMEZONE_OFFSET:
    if (len >= 3) {
      int16_t offset_min =
          (int16_t)((uint16_t)data[1] << 8 | (uint16_t)data[2]);
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
        LOG_INF("[SMF] cmd 0x05 timezone offset %d min", (int)offset_min);
        display_show_last_cleaned();
      } else {
        LOG_WRN("[SMF] cmd 0x05 timezone store failed: %d", ret);
      }
    } else {
      LOG_WRN("[SMF] cmd 0x05 timezone: len %u < 3", len);
    }
    break;
  default:
    LOG_WRN("[SMF] unknown downlink cmd 0x%02X", cmd);
    break;
  }
}

/* Set when SMF_EVT_SYSTEM_READY received; gates Normal-mode actions until "go".
 */
static volatile bool system_ready;

static void smf_thread_fn(void *a, void *b, void *c) {
  smf_msg_t msg;
  enum system_mode mode = MODE_NORMAL;

  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  LOG_INF("[SMF] thread started, state=%s", smf_mode_str(mode));

  while (1) {
    if (k_msgq_get(&smf_msgq, &msg, K_FOREVER) != 0) {
      continue;
    }

    /* System go: all inits and threads started. */
    if (msg.ev_type == SMF_EVT_SYSTEM_READY) {
      system_ready = true;
      LOG_INF("[SMF] system ready (all go)");
      continue;
    }

    LOG_DBG("[SMF] dequeue ev=%s button_id=%u ts=%lld (mode=%s)",
            smf_ev_type_str(msg.ev_type), msg.button_id, msg.timestamp_ms,
            smf_mode_str(mode));

    switch (mode) {
    case MODE_NORMAL:
      if (msg.ev_type >= SMF_EVT_BUTTON_SINGLE_0 &&
          msg.ev_type <= SMF_EVT_BUTTON_SINGLE_5) {
        if (!system_ready) {
          LOG_DBG("[SMF] Normal: button %u ignored (system not ready yet)",
                  msg.button_id);
        } else {
          LOG_DBG("[SMF] state=Normal -> app_logic_public_vote(button_id=%u)",
                  msg.button_id);
          rail_manager_request_3v3a();
          app_logic_public_vote(msg.button_id);
          rail_manager_release_3v3a();
        }
      } else if (msg.ev_type == SMF_EVT_JOINED) {
        LOG_DBG("[SMF] state=Normal -> JOINED -> counter_sync");
        rail_manager_request_3v3a();
        k_msleep(5000);
        smf_do_counter_sync(false); /* confirmed on rejoin */
        display_show_last_cleaned();
        rail_manager_release_3v3a();

      } else if (msg.ev_type == SMF_EVT_JOIN_STARTED) {
        display_show_connecting();
        LOG_DBG("[SMF] LoRa join started (orchestration visibility)");
      } else if (msg.ev_type == SMF_EVT_JOIN_CYCLE_FAILED) {
        display_show_last_cleaned();
        display_request_full_refresh(); /* Force EPD full refresh so panel
                                           updates from LOGO */
        LOG_INF("[SMF] Join cycle failed -> Last Cleaned (customer-facing)");
      } else if (msg.ev_type == SMF_EVT_TIME_SYNC_DONE) {
        LOG_DBG("[SMF] LoRa time sync done, ok=%d", (msg.button_id == 0));
        if (housekeeping_holding_3v3a) {
          housekeeping_holding_3v3a = false;
          rail_manager_release_3v3a();
          rail_manager_release_3v3(); // Release both
        }
      } else if (msg.ev_type == SMF_EVT_HOUSEKEEPING_TICK) {
        /* Housekeeping runs under SMF rail arbitration; hold 3.3A until
         * TIME_SYNC_DONE. Within this window:
         *  - sample battery via ADC and enqueue EVT_BATTERY_STATUS (0x10)
         *  - request time sync (DeviceTimeReq/Ans)
         *  - request LinkCheckReq MAC command
         *
         * LoRa thread and time_sync module own lorawan_* and RTC writes; SMF
         * just sequences requests and owns the 3.3A rail.
         */
        if (lora_is_joined()) {
          housekeeping_holding_3v3a = true;
          rail_manager_request_3v3a();
          rail_manager_request_3v3(); // MUST BE ON for the divider
          /* 500ms is great; gives the main rail and capacitors time to charge
           */
          k_msleep(500);

          /* 1) Battery status heartbeat uplink (EVT_BATTERY_STATUS). */
          int32_t battery_mv = 0;
          if (battery_adc_read_mv(&battery_mv) == 0 && battery_mv > 0) {
            uint32_t epoch_s = 0;
            (void)rtc_get_epoch_seconds(&epoch_s);
            if (epoch_s == 0) {
              epoch_s = (uint32_t)(k_uptime_get() / 1000U);
            }

            uint8_t payload[PAYLOAD_LEN_BYTES];
            int pret = payload_gen_build_battery_status(
                epoch_s, (uint16_t)battery_mv, 0 /* percent */, 0 /* flags */,
                payload);
            if (pret == 0) {
              lora_uplink_msg_t msg_hk = (lora_uplink_msg_t){0};
              msg_hk.port = FPORT_HOUSEKEEPING;
              msg_hk.confirmed = true; /* heartbeat battery status can be
                                          unconfirmed but i set it to true*/
              msg_hk.len = PAYLOAD_LEN_BYTES;
              memcpy(msg_hk.data, payload, PAYLOAD_LEN_BYTES);
              int qret = lora_put_event(&msg_hk, K_MSEC(500));
              if (qret == 0) {
                LOG_INF("[SMF] housekeeping: queued battery status %d mV",
                        battery_mv);
              } else {
                LOG_WRN("[SMF] housekeeping: lora_put_event battery failed: %d",
                        qret);
              }
            } else {
              LOG_WRN("[SMF] housekeeping: build battery status failed: %d",
                      pret);
            }
          } else {
            LOG_WRN("[SMF] housekeeping: ADC battery read failed");
          }

          /* 2. NOW trigger the async events that might signal "Done" */
          lora_request_link_check(true);
          lora_request_time_sync();

          /* 3. Counter sync (confirmed, like heartbeat and rejoin path) */
          smf_do_counter_sync(true);
          rail_manager_release_3v3();
          rail_manager_release_3v3a();
        }
      } else if (msg.ev_type == SMF_EVT_DOWNLINK) {
        k_mutex_lock(&smf_dl_mutex, K_FOREVER);
        smf_handle_downlink(msg.payload.downlink.port, msg.payload.downlink.len,
                            smf_dl_payload);
        k_mutex_unlock(&smf_dl_mutex);
      } else if (msg.ev_type == SMF_EVT_COMBO_STAFF) {
        mode = MODE_STAFF;
        LOG_INF("[SMF] Normal -> Staff (LED solid, 20s timeout)");
        rail_manager_request_3v3a();
        (void)led_manager_show(0, LED_PATTERN_ON);
        mode_timeout_ev = SMF_EVT_STAFF_TIMEOUT;
        k_timer_start(&mode_timeout_timer, K_MSEC(STAFF_TIMEOUT_MS), K_NO_WAIT);
      } else if (msg.ev_type == SMF_EVT_COMBO_DEVICE_INFO ||
                 msg.ev_type == SMF_EVT_COMBO_JOIN ||
                 msg.ev_type == SMF_EVT_COMBO_REBOOT) {
        LOG_INF("[SMF] state=Normal -> %s ignored (enter Staff first)",
                smf_ev_type_str(msg.ev_type));
      }
      break;

    case MODE_STAFF:
      if (msg.ev_type == SMF_EVT_STAFF_TIMEOUT) {
        mode = MODE_NORMAL;
        k_timer_stop(&mode_timeout_timer);
        (void)led_manager_show(0, LED_PATTERN_OFF);
        LOG_INF("[SMF] Staff -> Normal (timeout)");
      } else if (msg.ev_type == SMF_EVT_COMBO_JOIN) {
        mode = MODE_NORMAL;
        k_timer_stop(&mode_timeout_timer);
        (void)led_manager_show(0, LED_PATTERN_OFF);
        LOG_INF("[SMF] Staff -> Normal (deliberate join; trigger LoRa join)");
        display_show_connecting();
        lora_request_join();
      } else if (msg.ev_type == SMF_EVT_COMBO_REBOOT) {
        mode = MODE_REBOOT;
        k_timer_stop(&mode_timeout_timer);
        LOG_INF("[SMF] Staff -> Reboot (LED 3s then reboot)");
        (void)led_manager_show(0, LED_PATTERN_POWER_ON);
        k_timer_start(&reboot_timer, K_MSEC(REBOOT_LED_MS), K_NO_WAIT);
      } else if (msg.ev_type == SMF_EVT_COMBO_DEVICE_INFO) {
        mode = MODE_DEVICE_INFO;
        k_timer_stop(&mode_timeout_timer);
        (void)led_manager_show(0, LED_PATTERN_OFF);
        display_show_device_info();
        LOG_INF("[SMF] Staff -> DeviceInfo (30s timeout)");
        mode_timeout_ev = SMF_EVT_DEVICE_INFO_TIMEOUT;
        k_timer_start(&mode_timeout_timer, K_MSEC(DEVICE_INFO_TIMEOUT_MS),
                      K_NO_WAIT);
      } else if (msg.ev_type == SMF_EVT_COMBO_STAFF) {
        LOG_DBG("[SMF] Staff mode: COMBO_STAFF re-entry ignored (already in "
                "Staff)");
      } else if (msg.ev_type >= SMF_EVT_BUTTON_SINGLE_0 &&
                 msg.ev_type <= SMF_EVT_BUTTON_SINGLE_5) {
        uint8_t bid = (uint8_t)(msg.ev_type - SMF_EVT_BUTTON_SINGLE_0);
        uint8_t intent;
        if (bid == 0) {
          intent = NFC_INTENT_CHECK_IN;
        } else if (bid == 1) {
          intent = NFC_INTENT_CHECK_OUT;
        } else {
          intent = NFC_INTENT_VOTE;
        }
        mode = MODE_NFC_SCAN;
        k_timer_stop(&mode_timeout_timer);
        rail_manager_request_3v3a();
        rail_manager_request_3v6();
        (void)led_manager_show(0, LED_PATTERN_NFC_WAITING);
        mode_timeout_ev = SMF_EVT_NFC_TIMEOUT;
        k_timer_start(&mode_timeout_timer, K_MSEC(NFC_SCAN_TIMEOUT_MS),
                      K_NO_WAIT);
        nfc_scan_start(intent, bid);
        LOG_INF("[SMF] Staff -> NFCScan (button %u, intent %u)", bid, intent);
      } else {
        LOG_DBG("[SMF] Staff mode: event %s ignored",
                smf_ev_type_str(msg.ev_type));
      }
      break;

    case MODE_DEVICE_INFO:
      if (msg.ev_type == SMF_EVT_DEVICE_INFO_TIMEOUT) {
        mode = MODE_NORMAL;
        k_timer_stop(&mode_timeout_timer);
        LOG_INF("[SMF] DeviceInfo -> Normal (timeout)");
        display_show_last_cleaned();
      }
      break;

    case MODE_REBOOT:
      /* Reboot timer will fire; ignore other events */
      break;

    case MODE_NFC_SCAN:
      if (msg.ev_type == SMF_EVT_NFC_TIMEOUT) {
        nfc_scan_cancel();
        LOG_DBG("[SMF] NFC scan timeout (worker will post result)");
      } else if (msg.ev_type == SMF_EVT_NFC_RESULT) {
        k_timer_stop(&mode_timeout_timer);

        uint8_t ok = msg.payload.nfc.ok;
        uint8_t intent = msg.payload.nfc.intent;
        uint8_t bid = msg.payload.nfc.button_id;

        /* Read RTC while 3.3A is still held (RTC on I2C needs it). Release
         * rails after we have epoch and have queued display/uplink. */
        uint32_t epoch_s = 0;
        (void)rtc_get_epoch_seconds(&epoch_s);
        if (epoch_s == 0) {
          epoch_s = (uint32_t)(k_uptime_get() / 1000U);
        }

        rail_manager_release_3v6();
        rail_manager_release_3v3a();

        if (ok) {
          uint8_t payload[PAYLOAD_LEN_BYTES];
          uint8_t data_4[SMF_NFC_DATA_SIZE];
          k_mutex_lock(&smf_nfc_mutex, K_FOREVER);
          memcpy(data_4, smf_nfc_data, SMF_NFC_DATA_SIZE);
          k_mutex_unlock(&smf_nfc_mutex);

          if (intent == NFC_INTENT_CHECK_IN) {
            display_show_cleaning();
          } else if (intent == NFC_INTENT_CHECK_OUT) {
            (void)last_cleaned_store_set(epoch_s);
            display_show_last_cleaned();
          }

          int pret = -EINVAL;
          if (intent == NFC_INTENT_CHECK_IN) {
            pret = payload_gen_build_nfc_in(epoch_s, data_4, payload);
          } else if (intent == NFC_INTENT_CHECK_OUT) {
            pret = payload_gen_build_nfc_out(epoch_s, data_4, payload);
          } else {
            pret = payload_gen_build_nfc_vote(epoch_s, bid, data_4, payload);
          }
          if (pret == 0) {
            lora_uplink_msg_t uplink = {0};
            uplink.port = FPORT_NFC;
            uplink.confirmed = true; // NFC should be confirmed
            uplink.len = PAYLOAD_LEN_BYTES;
            memcpy(uplink.data, payload, PAYLOAD_LEN_BYTES);
            if (lora_put_event(&uplink, K_MSEC(500)) == 0) {
              LOG_INF("[SMF] NFC uplink queued (intent=%u)", intent);
            }
          }
          (void)led_manager_show(0, LED_PATTERN_CONFIRM);
        } else {
          (void)led_manager_show(0, LED_PATTERN_NFC_FAIL);
        }
        mode = MODE_NORMAL;
        LOG_INF("[SMF] NFCScan -> Normal (ok=%u)", ok);
      }
      break;

    case MODE_PROCESS_ACTION:
      LOG_DBG("[SMF] state=%s (stub), ev=%s", smf_mode_str(mode),
              smf_ev_type_str(msg.ev_type));
      break;

    default:
      break;
    }
  }
}

K_THREAD_DEFINE(smf_thread_id, SMF_THREAD_STACK_SIZE, smf_thread_fn, NULL, NULL,
                NULL, SMF_THREAD_PRIORITY, 0, -1);

int smf_post_event(uint8_t ev_type, uint8_t button_id, int64_t timestamp_ms) {
  smf_msg_t msg = {
      .ev_type = ev_type,
      .button_id = button_id,
      .timestamp_ms = timestamp_ms,
  };
  return k_msgq_put(&smf_msgq, &msg, K_NO_WAIT) == 0 ? 0 : -ENOMEM;
}

int smf_post_downlink(uint8_t port, uint8_t len, const uint8_t *data) {
  if (data == NULL || len > LORA_MAX_PAYLOAD_SIZE) {
    return -EINVAL;
  }
  k_mutex_lock(&smf_dl_mutex, K_FOREVER);
  memcpy(smf_dl_payload, data, len);
  k_mutex_unlock(&smf_dl_mutex);

  smf_msg_t msg = {
      .ev_type = SMF_EVT_DOWNLINK,
      .button_id = 0,
      .timestamp_ms = k_uptime_get(),
  };
  msg.payload.downlink.port = port;
  msg.payload.downlink.len = len;

  int ret = k_msgq_put(&smf_msgq, &msg, K_NO_WAIT);
  return ret == 0 ? 0 : -ENOMEM;
}

int smf_post_nfc_result(uint8_t ok, uint8_t intent, uint8_t button_id,
                        const uint8_t *data_4) {
  if (data_4 != NULL) {
    k_mutex_lock(&smf_nfc_mutex, K_FOREVER);
    memcpy(smf_nfc_data, data_4, SMF_NFC_DATA_SIZE);
    k_mutex_unlock(&smf_nfc_mutex);
  }

  smf_msg_t msg = {
      .ev_type = SMF_EVT_NFC_RESULT,
      .button_id = 0,
      .timestamp_ms = k_uptime_get(),
  };
  msg.payload.nfc.ok = ok;
  msg.payload.nfc.intent = intent;
  msg.payload.nfc.button_id = button_id;

  int ret = k_msgq_put(&smf_msgq, &msg, K_NO_WAIT);
  return ret == 0 ? 0 : -ENOMEM;
}
