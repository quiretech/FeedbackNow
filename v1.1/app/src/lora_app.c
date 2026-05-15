#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>
#include "battery_adc.h"
#include "lora_app.h"
#include "lora_link_stats.h"
#include "mapek_link.h"
#include "smf_system_mode.h"
#include "time_sync.h"

LOG_MODULE_REGISTER(lora_app, CONFIG_LOG_DEFAULT_LEVEL);

// Ensure proper alignment for the message queue
K_MSGQ_DEFINE(lora_msgq, sizeof(lora_uplink_msg_t), LORA_MSGQ_SIZE, 4);

/* Command queue: SMF (or bootstrap) posts JOIN/TIME_SYNC; LoRa thread consumes
 * only. */
#define LORA_CMDQ_SIZE 4
K_MSGQ_DEFINE(lora_cmdq, sizeof(uint8_t), LORA_CMDQ_SIZE, 4);

/* Join status tracking */
atomic_t lora_joined_flag = ATOMIC_INIT(0);
K_SEM_DEFINE(lora_join_sem, 0, 1);
extern struct k_sem lora_ready_sem;

/* One-shot guard for DR-based time-sync retry per join lifecycle. */
static atomic_t dr_time_sync_retry_requested = ATOMIC_INIT(0);
static atomic_t latest_dr_seen = ATOMIC_INIT(-1);

int lora_cmd_put(uint8_t cmd) {
  if (cmd >= LORA_CMD_COUNT) {
    return -EINVAL;
  }
  int ret = k_msgq_put(&lora_cmdq, &cmd, K_NO_WAIT);
  if (ret != 0) {
    LOG_WRN("cmd queue full (cmd=%u)", cmd);
  }
  return ret;
}

void lora_request_join(void) { (void)lora_cmd_put(LORA_CMD_JOIN); }

void lora_request_time_sync(void) { (void)lora_cmd_put(LORA_CMD_TIME_SYNC); }

static void lora_burst_tail_time_sync_fn(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(lora_burst_tail_time_sync_w, lora_burst_tail_time_sync_fn);

static void lora_burst_tail_time_sync_fn(struct k_work *work) {
  ARG_UNUSED(work);

  LOG_DBG("counter-sync burst done; schedule DeviceTimeReq");

  (void)lora_cmd_put(LORA_CMD_TIME_SYNC);
}

void lora_schedule_time_sync_after_counter_burst(
    enum lora_burst_tail_profile profile) {

  uint32_t delay_ms = (profile == LORA_BURST_TAIL_HOUSEKEEPING)
                          ? LORA_POST_COUNTER_BURST_TIME_SYNC_DELAY_HK_MS
                          : LORA_POST_COUNTER_BURST_TIME_SYNC_DELAY_JOIN_MS;

  (void)k_work_cancel_delayable(&lora_burst_tail_time_sync_w);

  LOG_DBG("DeviceTimeReq deferred %u ms (profile=%d)", (unsigned)delay_ms,
          (int)profile);

  (void)k_work_schedule(&lora_burst_tail_time_sync_w, K_MSEC(delay_ms));
}

void lora_cancel_scheduled_burst_time_sync(void) {
  (void)k_work_cancel_delayable(&lora_burst_tail_time_sync_w);
}

void lora_request_enable_adr(void) {
  (void)lora_cmd_put(LORA_CMD_ENABLE_ADR);
}

void lora_request_link_check(bool force_request) {
  (void)lora_cmd_put(force_request ? LORA_CMD_LINK_CHECK_FORCE
                                   : LORA_CMD_LINK_CHECK);
}

void lora_reset_dr_time_sync_retry(void) {
  atomic_set(&dr_time_sync_retry_requested, 0);
}

void lora_on_join_success(void) {
  lora_reset_dr_time_sync_retry();
}

int lora_wait_until_ready(k_timeout_t timeout) {
  return k_sem_take(&lora_ready_sem, timeout);
}

bool lora_get_cmd(uint8_t *cmd_out, k_timeout_t timeout) {
  if (cmd_out == NULL) {
    return false;
  }
  int ret = k_msgq_get(&lora_cmdq, cmd_out, timeout);
  if (ret != 0) {
    return false;
  }
  return true;
}

/* Battery level callback for LoRaWAN MAC (0=external, 1..254 level, 255=unknown).
 * Value is cached when housekeeping reads ADC (slow path); this callback must
 * stay non-blocking for the LoRa stack.
 */
static uint8_t lora_battery_level_cb(void) {
  return battery_adc_lorawan_level_get();
}

bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout) {
  if (msg == NULL) {
    LOG_ERR("get_event: null msg pointer");
    return false;
  }

  int ret = k_msgq_get(&lora_msgq, msg, timeout);
  if (ret != 0) {
    if (ret != -EAGAIN && ret != -ENOMSG) {
      LOG_ERR("get_event failed: %d", ret);
    }
    return false;
  }
  return true;
}

int lora_put_event(const lora_uplink_msg_t *msg, k_timeout_t timeout) {
  if (msg == NULL) {
    LOG_ERR("put_event: null msg pointer");
    return -EINVAL;
  }

  /* Reject messages if not joined to network */
  if (!lora_is_joined()) {
    LOG_WRN("put_event: not joined");
    return -ENOTCONN;
  }

  if (msg->len > LORA_MAX_PAYLOAD_SIZE) {
    LOG_ERR("put_event: len %d > max %d", msg->len, LORA_MAX_PAYLOAD_SIZE);
    return -EINVAL;
  }

  int ret = k_msgq_put(&lora_msgq, msg, timeout);
  if (ret != 0) {
    LOG_ERR("uplink queue full (drop): %d", ret);
  }
  return ret;
}

/**
 * @brief Downlink callback
 */
void lora_app_dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr,
                          uint8_t len, const uint8_t *frmpayload) {
  const bool time_upd = !!(flags & LORAWAN_TIME_UPDATED);
  const int pend = (int)(flags & LORAWAN_DATA_PENDING);
  const int rssi_i = (int)rssi;
  const int snr_i = (int)snr;
  const int tu = time_upd ? 1 : 0;

  if (time_upd) {
    LOG_DBG("DeviceTimeAns (clock sync)");
    time_sync_on_lorawan_time_updated();
  }

  if (!frmpayload || len == 0U) {
    uint8_t mf = 0U;
    if (time_upd) {
      mf |= MAPEK_DL_FEED_LORAWAN_TIME_UPD;
    }
    mapek_link_feed_dl(rssi, snr, mf);
    /* MAC-only Rx (dwell / join): very frequent unless pending or clock sync */
    if (pend != 0 || time_upd) {
      LOG_INF("DL MAC p=%u pend=%d rssi=%d snr=%d tu=%d", (unsigned)port, pend,
              rssi_i, snr_i, tu);
    } else {
      LOG_DBG("DL MAC p=%u rssi=%d snr=%d", (unsigned)port, rssi_i, snr_i);
    }
    return;
  }

  if (len > LORA_MAX_DOWNLINK_FRMPAYLOAD_SIZE) {
    LOG_WRN("DL FRMPayload len=%u > max %u (truncate)", (unsigned)len,
            (unsigned)LORA_MAX_DOWNLINK_FRMPAYLOAD_SIZE);
    len = (uint8_t)LORA_MAX_DOWNLINK_FRMPAYLOAD_SIZE;
  }

  LOG_INF("DL app p=%u len=%u pend=%d rssi=%d snr=%d tu=%d", (unsigned)port,
          (unsigned)len, pend, rssi_i, snr_i, tu);
  LOG_HEXDUMP_DBG(frmpayload, len, "DL FRMPayload");

  uint8_t mf = MAPEK_DL_FEED_APP_PAYLOAD;
  if (time_upd) {
    mf |= MAPEK_DL_FEED_LORAWAN_TIME_UPD;
  }
  mapek_link_feed_dl(rssi, snr, mf);

  /* Post to SMF for command dispatch */
  if (smf_post_downlink(port, len, frmpayload) != 0) {
    LOG_WRN("smf_post_downlink failed");
  }
}

/**
 * @brief Data rate change callback
 */
void lora_app_dr_changed(enum lorawan_datarate dr) {
  uint8_t unused, max_size;

  lorawan_get_payload_sizes(&unused, &max_size);
  LOG_DBG("DR%d max_UL_FRMPayload=%u B", (int)dr, (unsigned)max_size);
  atomic_set(&latest_dr_seen, (atomic_val_t)dr);
}

/**
 * @brief Initialize LoRaWAN stack and register callbacks
 */
int lora_app_init(void) {
  int ret;

  mapek_link_init();

  const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
  if (!device_is_ready(lora_dev)) {
    LOG_ERR("%s not ready", lora_dev->name);
    return -ENODEV;
  }

  ret = lorawan_start();
  if (ret < 0) {
    LOG_ERR("lorawan_start failed: %d", ret);
    return ret;
  }

  // // NEW CHANNEL MASK SUB BAND 2
  //   uint16_t mask[6] = {
  //     0xFF00,
  //     0x0000,
  //     0x0000,
  //     0x0000,
  //     0x0000,
  //     0x0000
  // };

  // int ret_mask = lorawan_set_channels_mask(mask, ARRAY_SIZE(mask));
  // if (ret_mask < 0) {
  //     LOG_ERR("Failed to set channel mask: %d", ret_mask);
  // } else {
  //     LOG_INF("Channel mask set to FSB2 (channels 8–15)");
  // }

  static struct lorawan_downlink_cb dl_cb = {.port = LW_RECV_PORT_ANY,
                                             .cb = lora_app_dl_callback};
  lorawan_register_downlink_callback(&dl_cb);

  lorawan_register_dr_changed_callback(lora_app_dr_changed);

  /* Register battery level callback (cached from last ADC read; 255 unknown). */
  lorawan_register_battery_level_callback(lora_battery_level_cb);

  /* LinkCheckAns capture: (demod_margin, nb_gateways) into atomic snapshot.
   * Init clears stats before registering so a stale sample can't leak across
   * a soft reboot in the same RAM image. */
  lora_link_stats_init();
  lora_link_stats_register();

  /* ADR off until OTAA succeeds; enabled in run_join_cycle so LinkADRReq can
   * steer DR during the post-join / HK uplink bursts before DeviceTimeReq. */

  lorawan_enable_adr(false);

#if defined(CONFIG_LORAMAC_REGION_EU868) && defined(CONFIG_LORAMAC_REGION_US915)
  const char *region = "EU868+US915";
#elif defined(CONFIG_LORAMAC_REGION_EU868)
  const char *region = "EU868";
#elif defined(CONFIG_LORAMAC_REGION_US915)
  const char *region = "US915";
#else
  const char *region = "?";
#endif
  LOG_INF("LoRaWAN ok %s region=%s adr=off-until-join", lora_dev->name,
          region);
  return 0;
}
