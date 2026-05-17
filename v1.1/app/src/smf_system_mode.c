/**
 * System mode FSM: single thread, single input queue (per architecture).
 * States: Normal, Staff, NFCScan, DeviceInfo, Reboot, ProcessAction.
 * Implements: timeouts (Staff 20s, DeviceInfo 30s), Staff-first for
 * Join/Reboot.
 *
 * Power gating: SMF owns 3.3A (peripheral rail) for all flows it dispatches.
 * request_3v3a() before LED/EEPROM/RTC work, release_3v3a() when leaving the
 * activity or mode. LoRa thread and async EEPROM flush keep their own
 * request/release where they are not driven by SMF.
 */
#include "smf_system_mode.h"
#include "app_logic.h"
#include "boot_info.h"
#include "counter_sync.h"
#include "downlink_dispatch.h"
#include "display_manager.h"
#include "housekeeping.h"
#include "last_cleaned_store.h"
#include "led_manager.h"
#include "lora_app.h"
#include "nfc_service.h"
#include "payload_gen.h"
#include "rail_manager.h"
#include "rtc.h"
#include "sys_config.h"

#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(smf, CONFIG_LOG_DEFAULT_LEVEL);

/** Queue NFC success LED burst; block until it can finish, then SMF may drive
 *  EPD (contrast: slow “scanning” pulse vs fast “found it” burst). */
static void smf_nfc_success_led_hold_for_epd(void) {
  (void)led_manager_show(0, LED_PATTERN_CONFIRM);
  k_msleep(LED_CONFIRM_SMF_BLOCK_MS);
}

enum system_mode {
  MODE_NORMAL,
  MODE_STAFF,
  MODE_NFC_SCAN,
  MODE_DEVICE_INFO,
  MODE_REBOOT,
  MODE_PROCESS_ACTION,
  MODE_COUNT
};

/* Use sys_config.h for SMF_MSGQ_SIZE, SMF_MSGQ_ALIGN, SMF_THREAD_STACK_SIZE */

K_MSGQ_DEFINE(smf_msgq, sizeof(smf_msg_t), SMF_MSGQ_SIZE, SMF_MSGQ_ALIGN);

#define SMF_THREAD_PRIORITY 6

static atomic_t smf_msgq_drop_count = ATOMIC_INIT(0);
static atomic_t smf_msgq_peak_used = ATOMIC_INIT(0);

static bool smf_ev_best_effort_only(uint8_t ev_type) {
  return ev_type == SMF_EVT_HOUSEKEEPING_TICK;
}

static void smf_msgq_peak_note(void) {
  uint32_t u = (uint32_t)k_msgq_num_used_get(&smf_msgq);
  for (;;) {
    atomic_val_t peak = atomic_get(&smf_msgq_peak_used);
    if (u <= (uint32_t)peak) {
      return;
    }
    if (atomic_cas(&smf_msgq_peak_used, peak, (atomic_val_t)u)) {
      return;
    }
  }
}

/* Mode timeout: timer posts this event so SMF returns to Normal (atomic: timer
 * vs SMF thread) */
static atomic_t mode_timeout_ev = ATOMIC_INIT(0); /* 0 = SMF_EVT_NONE */
static void smf_housekeeping_work_handler(struct k_work *work);
static void smf_join_started_ui_work_handler(struct k_work *work);
static void smf_join_failed_ui_work_handler(struct k_work *work);
static void mode_timeout_work_handler(struct k_work *work);
static void reboot_work_handler(struct k_work *work);
K_WORK_DEFINE(smf_housekeeping_work, smf_housekeeping_work_handler);
K_WORK_DEFINE(smf_join_started_ui_work, smf_join_started_ui_work_handler);
K_WORK_DEFINE(smf_join_failed_ui_work, smf_join_failed_ui_work_handler);
K_WORK_DEFINE(mode_timeout_work, mode_timeout_work_handler);
K_WORK_DEFINE(reboot_work, reboot_work_handler);

/* k_timer expiry runs in ISR context; defer SMF post / reboot to system workqueue.
 */
static void mode_timeout_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);
  (void)k_work_submit(&mode_timeout_work);
}

static void mode_timeout_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  int ev = atomic_get(&mode_timeout_ev);
  if (ev != SMF_EVT_NONE) {
    (void)smf_post_event((uint8_t)ev, 0, k_uptime_get());
    atomic_set(&mode_timeout_ev, SMF_EVT_NONE);
  }
}

static void reboot_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);
  (void)k_work_submit(&reboot_work);
}

static void reboot_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  LOG_INF("smf cold_reboot");
  sys_reboot(SYS_REBOOT_COLD);
}

static void smf_join_started_ui_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  display_show_connecting();
}

static void smf_join_failed_ui_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  display_show_last_cleaned();
  display_request_full_refresh();
}

#if EPD_ENABLED
static bool device_status_shown_this_boot;

static void smf_show_device_status_with_dwell(void (*overlap_work)(void)) {
  int64_t t0 = k_uptime_get();
  display_show_device_status_sync();
  if (overlap_work != NULL) {
    overlap_work();
  }
  int64_t elapsed = k_uptime_get() - t0;
  if (elapsed < (int64_t)EPD_DEVICE_STATUS_COMMISSION_MS) {
    uint32_t remaining =
        (uint32_t)((int64_t)EPD_DEVICE_STATUS_COMMISSION_MS - elapsed);
    LOG_DBG("device_status dwell: %u ms remaining (%lld ms used)",
            (unsigned)remaining, (long long)elapsed);
    k_msleep((int32_t)remaining);
  } else {
    LOG_DBG("device_status dwell: overlap %lld ms >= %u ms",
            (long long)elapsed, (unsigned)EPD_DEVICE_STATUS_COMMISSION_MS);
  }
}

static void smf_joined_overlap_work(void) {
  counter_sync_run(LORA_COUNTER_SYNC_CONFIRMED);
  downlink_queue_housekeeping_state_snapshot();
  lora_schedule_time_sync_after_counter_burst(LORA_BURST_TAIL_JOIN_POST);
}
#endif /* EPD_ENABLED */

K_TIMER_DEFINE(mode_timeout_timer, mode_timeout_expiry, NULL);
K_TIMER_DEFINE(reboot_timer, reboot_expiry, NULL);

static void smf_dl_schedule_reboot_led_ms(uint32_t led_ms) {
  (void)led_manager_show(0, LED_PATTERN_POWER_ON);
  k_timer_start(&reboot_timer, K_MSEC(led_ms), K_NO_WAIT);
}

static const struct downlink_dispatch_ops smf_dl_ops = {
    .schedule_reboot_led_ms = smf_dl_schedule_reboot_led_ms,
};

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
  case SMF_EVT_DL_REBOOT:
    return "DL_REBOOT";
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

static void smf_housekeeping_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  housekeeping_run();
}

/* Set when SMF_EVT_SYSTEM_READY received; gates Normal-mode actions until "go".
 */
static volatile bool system_ready;
K_SEM_DEFINE(smf_ready_sem, 0, 1);

static void smf_thread_fn(void *a, void *b, void *c) {
  smf_msg_t msg;
  enum system_mode mode = MODE_NORMAL;

  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  LOG_DBG("SMF start state=%s", smf_mode_str(mode));

  while (1) {
    if (k_msgq_get(&smf_msgq, &msg, K_FOREVER) != 0) {
      continue;
    }

    /* System go: all inits and threads started. */
    if (msg.ev_type == SMF_EVT_SYSTEM_READY) {
      system_ready = true;
      k_sem_give(&smf_ready_sem);
      LOG_INF("smf system_ready");
      continue;
    }

    LOG_DBG("dequeue ev=%s button_id=%u ts=%lld (mode=%s)",
            smf_ev_type_str(msg.ev_type), msg.button_id, msg.timestamp_ms,
            smf_mode_str(mode));

    if (msg.ev_type == SMF_EVT_DL_REBOOT) {
      k_timer_stop(&mode_timeout_timer);
      if (mode == MODE_NFC_SCAN) {
        nfc_scan_cancel();
        rail_manager_release_3v6();
        rail_manager_release_3v3a(); /* NFC ref */
        rail_manager_release_3v3a(); /* Staff ref (entered NFC from Staff) */
      } else if (mode == MODE_STAFF) {
        rail_manager_release_3v3a(); /* paired with request on enter Staff */
      }
      mode = MODE_REBOOT;
      (void)led_manager_show(0, LED_PATTERN_POWER_ON);
      k_timer_start(&reboot_timer, K_MSEC(REBOOT_LED_MS), K_NO_WAIT);
      LOG_INF("smf dl_reboot led_ms=%u", (unsigned)REBOOT_LED_MS);
      continue;
    }

    switch (mode) {
    case MODE_NORMAL:
      if (msg.ev_type >= SMF_EVT_BUTTON_SINGLE_0 &&
          msg.ev_type <= SMF_EVT_BUTTON_SINGLE_5) {
        if (!system_ready) {
          LOG_DBG("Normal: button %u ignored (system not ready yet)",
                  msg.button_id);
        } else {
          /* Must run on SMF thread, not sysworkq: display_show_thanks_sync()
           * schedules display_work on the system workqueue and blocks on a
           * sem — same thread would deadlock until thanks sync timeout.
           */
          LOG_DBG("state=Normal -> app_logic_public_vote(button_id=%u)",
                  msg.button_id);
          rail_manager_request_3v3a();
          app_logic_public_vote(msg.button_id);
          rail_manager_release_3v3a();
        }
      } else if (msg.ev_type == SMF_EVT_JOINED) {
        /* Run on SMF thread (not system workqueue): display_show_last_cleaned_sync
         * blocks on display_work; same-thread would deadlock if submitted from
         * k_work. button_id: 1 = installer join LED already queued — pause so
         * burst finishes before LAST_CLEANED; 0 = silent join, no pause. */
        LOG_DBG("state=Normal -> JOINED -> last_cleaned + counter_sync");
        rail_manager_request_3v3a();
        if (msg.button_id != 0) {
          k_msleep(POST_JOIN_LED_BEFORE_EPD_MS);
        }

#if EPD_ENABLED
        if (!device_status_shown_this_boot && boot_info_is_commission_boot()) {
          device_status_shown_this_boot = true;
          LOG_INF("smf device_status cause=%s", boot_info_cause_str());
          smf_show_device_status_with_dwell(smf_joined_overlap_work);
          display_show_last_cleaned_sync();
        } else
#endif
        {
          display_show_last_cleaned_sync();
          counter_sync_run(LORA_COUNTER_SYNC_CONFIRMED);

          downlink_queue_housekeeping_state_snapshot();

          lora_schedule_time_sync_after_counter_burst(LORA_BURST_TAIL_JOIN_POST);

        }
        rail_manager_release_3v3a();

      } else if (msg.ev_type == SMF_EVT_JOIN_STARTED) {
        (void)k_work_submit(&smf_join_started_ui_work);
        LOG_DBG("LoRa join started (orchestration visibility)");
      } else if (msg.ev_type == SMF_EVT_JOIN_CYCLE_FAILED) {
#if EPD_ENABLED
        if (!device_status_shown_this_boot && boot_info_is_commission_boot()) {
          device_status_shown_this_boot = true;
          LOG_INF("smf device_status join_fail cause=%s",
                  boot_info_cause_str());
          rail_manager_request_3v3a();
          smf_show_device_status_with_dwell(NULL);
          display_show_last_cleaned_sync();
          display_request_full_refresh();
          rail_manager_release_3v3a();
        } else
#endif
        {
          (void)k_work_submit(&smf_join_failed_ui_work);
        }
        LOG_INF("smf join_fail->last_cleaned");
      } else if (msg.ev_type == SMF_EVT_TIME_SYNC_DONE) {
        LOG_DBG("LoRa time sync done, ok=%d", (msg.button_id == 0));
        housekeeping_on_time_sync_done();
      } else if (msg.ev_type == SMF_EVT_HOUSEKEEPING_TICK) {
        (void)k_work_submit(&smf_housekeeping_work);
      } else if (msg.ev_type == SMF_EVT_DOWNLINK) {
        downlink_dispatch(msg.payload.downlink.port, msg.payload.downlink.len,
                          msg.payload.downlink.data, &smf_dl_ops);
      } else if (msg.ev_type == SMF_EVT_COMBO_STAFF) {
        mode = MODE_STAFF;
        LOG_INF("smf Norm->Staff t_ms=%u", (unsigned)STAFF_TIMEOUT_MS);
        rail_manager_request_3v3a();
        (void)led_manager_show(0, LED_PATTERN_ON);
        atomic_set(&mode_timeout_ev, SMF_EVT_STAFF_TIMEOUT);
        k_timer_start(&mode_timeout_timer, K_MSEC(STAFF_TIMEOUT_MS), K_NO_WAIT);
      } else if (msg.ev_type == SMF_EVT_COMBO_DEVICE_INFO ||
                 msg.ev_type == SMF_EVT_COMBO_JOIN ||
                 msg.ev_type == SMF_EVT_COMBO_REBOOT) {
        LOG_INF("smf ignore %s (staff_first)", smf_ev_type_str(msg.ev_type));
      }
      break;

    case MODE_STAFF:
      if (msg.ev_type == SMF_EVT_STAFF_TIMEOUT) {
        mode = MODE_NORMAL;
        k_timer_stop(&mode_timeout_timer);
        (void)led_manager_show(0, LED_PATTERN_OFF);
        rail_manager_release_3v3a(); /* paired with request on enter Staff */
        LOG_INF("smf Staff->Norm timeout");
      } else if (msg.ev_type == SMF_EVT_COMBO_JOIN) {
        mode = MODE_NORMAL;
        k_timer_stop(&mode_timeout_timer);
        (void)led_manager_show(0, LED_PATTERN_OFF);
        rail_manager_release_3v3a(); /* paired with request on enter Staff */
        LOG_INF("smf Staff->Norm join_req");
        (void)k_work_submit(&smf_join_started_ui_work);
        lora_request_join();
      } else if (msg.ev_type == SMF_EVT_COMBO_REBOOT) {
        mode = MODE_REBOOT;
        k_timer_stop(&mode_timeout_timer);
        rail_manager_release_3v3a(); /* Staff ref; reboot will reset system */
        LOG_INF("smf Staff->Reboot led_ms=%u", (unsigned)REBOOT_LED_MS);
        (void)led_manager_show(0, LED_PATTERN_POWER_ON);
        k_timer_start(&reboot_timer, K_MSEC(REBOOT_LED_MS), K_NO_WAIT);
      } else if (msg.ev_type == SMF_EVT_COMBO_DEVICE_INFO) {
        mode = MODE_DEVICE_INFO;
        k_timer_stop(&mode_timeout_timer);
        (void)led_manager_show(0, LED_PATTERN_OFF);
        rail_manager_release_3v3a(); /* Staff no longer needs LED rail */
        /* EPD immediately after LED/rail (sync flush; async used 5s RX delay). */
        display_show_device_status_sync();
        LOG_INF("smf Staff->DevInfo t_ms=%u", DEVICE_INFO_TIMEOUT_MS);
        atomic_set(&mode_timeout_ev, SMF_EVT_DEVICE_INFO_TIMEOUT);
        k_timer_start(&mode_timeout_timer, K_MSEC(DEVICE_INFO_TIMEOUT_MS),
                      K_NO_WAIT);
      } else if (msg.ev_type == SMF_EVT_COMBO_STAFF) {
        LOG_DBG("Staff mode: COMBO_STAFF re-entry ignored (already in "
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
        atomic_set(&mode_timeout_ev, SMF_EVT_NFC_TIMEOUT);
        k_timer_start(&mode_timeout_timer, K_MSEC(NFC_SCAN_TOTAL_MS),
                      K_NO_WAIT);
        nfc_scan_start(intent, bid);
        LOG_INF("smf Staff->NFC btn=%u intent=%u", bid, intent);
      } else {
        LOG_DBG("Staff mode: event %s ignored",
                smf_ev_type_str(msg.ev_type));
      }
      break;

    case MODE_DEVICE_INFO:
      if (msg.ev_type == SMF_EVT_DEVICE_INFO_TIMEOUT) {
        mode = MODE_NORMAL;
        k_timer_stop(&mode_timeout_timer);
        LOG_INF("smf DevInfo->Norm timeout");
        display_show_last_cleaned_sync();
        /* No rail to release: we released 3.3A when leaving Staff for
         * DeviceInfo */
      }
      break;

    case MODE_REBOOT:
      /* Reboot timer will fire; ignore other events */
      break;

    case MODE_NFC_SCAN:
      if (msg.ev_type == SMF_EVT_NFC_TIMEOUT) {
        nfc_scan_cancel();
        rail_manager_release_3v6();
        rail_manager_release_3v3a(); /* NFC ref */
        rail_manager_release_3v3a(); /* Staff ref (we entered NFC from Staff) */
        mode = MODE_NORMAL;
        (void)led_manager_show(0, LED_PATTERN_OFF);
        LOG_INF("NFCScan -> Normal (timeout)");
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
        rail_manager_release_3v3a(); /* NFC ref */
        rail_manager_release_3v3a(); /* Staff ref (we entered NFC from Staff) */

        if (ok) {
          uint8_t payload[PAYLOAD_LEN_BYTES];
          uint8_t data_4[4];
          memcpy(data_4, msg.payload.nfc.data_4, sizeof(data_4));

          if (intent == NFC_INTENT_CHECK_IN) {
            smf_nfc_success_led_hold_for_epd();
#if EPD_ENABLED
            rail_manager_request_3v3a();
            display_show_cleaning_sync();
            rail_manager_release_3v3a();
#endif
          } else if (intent == NFC_INTENT_CHECK_OUT) {
            /* Success burst on LED thread, then EPD once burst has finished. */
            smf_nfc_success_led_hold_for_epd();
            rail_manager_request_3v3a();
            (void)last_cleaned_store_set(epoch_s);
#if EPD_ENABLED
            display_show_last_cleaned_sync();
#endif
            rail_manager_release_3v3a();
          } else {
            /* Vote: success LED immediately (no EPD in this path). */
            smf_nfc_success_led_hold_for_epd();
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
            uplink.confirmed = LORA_NFC_UPLINK_CONFIRMED;
            uplink.len = PAYLOAD_LEN_BYTES;
            memcpy(uplink.data, payload, PAYLOAD_LEN_BYTES);
            LOG_INF("smf nfc payload intent=%u btn=%u len=%u", intent, bid,
                    (unsigned)uplink.len);
            LOG_HEXDUMP_INF(uplink.data, uplink.len, "NFC UL payload");
            if (lora_is_joined()) {
              if (lora_put_event(&uplink, K_MSEC(500)) == 0) {
                LOG_INF("smf nfc_ul ok intent=%u", intent);
              } else {
                LOG_WRN("smf nfc_ul fail intent=%u", intent);
              }
            } else {
              LOG_INF("smf nfc_ul skip_no_join intent=%u", intent);
            }
          }
        } else {
          (void)led_manager_show(0, LED_PATTERN_NFC_FAIL);
        }
        mode = MODE_NORMAL;
        LOG_INF("smf NFC->Norm ok=%u", ok);
      }
      break;

    case MODE_PROCESS_ACTION:
      LOG_DBG("state=%s (stub), ev=%s", smf_mode_str(mode),
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
  k_timeout_t t = smf_ev_best_effort_only(ev_type)
                    ? K_NO_WAIT
                    : K_MSEC(SMF_POST_EVENT_CRITICAL_TIMEOUT_MS);
  int ret = k_msgq_put(&smf_msgq, &msg, t);
  if (ret != 0) {
    (void)atomic_add(&smf_msgq_drop_count, 1);
    LOG_WRN("msgq full, dropping ev=%u (ret=%d)", ev_type, ret);
    return -ENOMEM;
  }
  smf_msgq_peak_note();
  return 0;
}

int smf_post_downlink(uint8_t port, uint8_t len, const uint8_t *data) {
  if (data == NULL || len > LORA_MAX_DOWNLINK_FRMPAYLOAD_SIZE) {
    return -EINVAL;
  }

  smf_msg_t msg = {
      .ev_type = SMF_EVT_DOWNLINK,
      .button_id = 0,
      .timestamp_ms = k_uptime_get(),
  };
  msg.payload.downlink.port = port;
  msg.payload.downlink.len = len;
  memcpy(msg.payload.downlink.data, data, len);

  int ret = k_msgq_put(&smf_msgq, &msg, K_MSEC(SMF_POST_EVENT_CRITICAL_TIMEOUT_MS));
  if (ret != 0) {
    (void)atomic_add(&smf_msgq_drop_count, 1);
    LOG_WRN("msgq full, dropping downlink (ret=%d)", ret);
  } else {
    smf_msgq_peak_note();
  }
  return ret == 0 ? 0 : -ENOMEM;
}

int smf_post_nfc_result(uint8_t ok, uint8_t intent, uint8_t button_id,
                        const uint8_t *data_4) {
  smf_msg_t msg = {
      .ev_type = SMF_EVT_NFC_RESULT,
      .button_id = 0,
      .timestamp_ms = k_uptime_get(),
  };
  msg.payload.nfc.ok = ok;
  msg.payload.nfc.intent = intent;
  msg.payload.nfc.button_id = button_id;
  if (data_4 != NULL) {
    memcpy(msg.payload.nfc.data_4, data_4, sizeof(msg.payload.nfc.data_4));
  } else {
    memset(msg.payload.nfc.data_4, 0, sizeof(msg.payload.nfc.data_4));
  }

  int ret = k_msgq_put(&smf_msgq, &msg, K_MSEC(SMF_POST_EVENT_CRITICAL_TIMEOUT_MS));
  if (ret != 0) {
    (void)atomic_add(&smf_msgq_drop_count, 1);
    LOG_WRN("msgq full, dropping nfc result (ret=%d)", ret);
  } else {
    smf_msgq_peak_note();
  }
  return ret == 0 ? 0 : -ENOMEM;
}

uint32_t smf_msgq_peak_used_get(void) {
  return (uint32_t)atomic_get(&smf_msgq_peak_used);
}

uint32_t smf_msgq_drop_count_get(void) {
  return (uint32_t)atomic_get(&smf_msgq_drop_count);
}

int smf_wait_until_ready(k_timeout_t timeout) {
  return k_sem_take(&smf_ready_sem, timeout);
}
