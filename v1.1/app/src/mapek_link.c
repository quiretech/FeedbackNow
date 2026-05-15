/**
 * MAPE-K link Monitor: LinkCheck + DL RSSI/SNR + join edges, fixed-point EWMA.
 */
#include "mapek_link.h"

#include "sys_config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdint.h>

LOG_MODULE_REGISTER(mapek_link, CONFIG_LOG_DEFAULT_LEVEL);

static K_MUTEX_DEFINE(mon_mutex);

/** Unsigned EWMA: state_q8 < 0 => uninitialized. Signed RSSI/SNR: use INT32_MIN. */
static int32_t ewma_margin_q8 = -1;
static int32_t ewma_nb_gw_q8 = -1;
static int32_t ewma_rssi_q8 = INT32_MIN;
static int32_t ewma_snr_q8 = INT32_MIN;

static bool mon_joined;
static uint32_t mon_join_transition_ms;

static bool lc_have;
static uint8_t lc_last_margin;
static uint8_t lc_last_nb_gw;
static uint32_t lc_last_uptime_ms;

static bool dl_have;
static int16_t dl_last_rssi;
static int8_t dl_last_snr;
static uint32_t dl_last_uptime_ms;
static bool dl_last_app;
static bool dl_last_time_updated;

static int16_t mcps_last_ret;
static uint8_t mcps_last_class;
static uint8_t mcps_last_kind;
static uint8_t mcps_last_port;
static uint8_t mcps_last_len;
static bool mcps_last_confirmed;
static uint32_t mcps_last_uptime_ms;
static uint32_t mcps_cnt_app_ok;
static uint32_t mcps_cnt_app_fail;
static uint32_t mcps_cnt_lc_ok;
static uint32_t mcps_cnt_lc_fail;

static bool lc_req_pending;
static uint32_t lc_req_sent_ms;

static void monitor_log_inf_locked(void);

static uint8_t classify_mcps_ret(int ret) {
  if (ret == 0) {
    return (uint8_t)MAPEK_MCPS_CLASS_OK;
  }
  if (ret == MAPEK_LORAWAN_MCPS_RX2_TIMEOUT_ERRNO) {
    return (uint8_t)MAPEK_MCPS_CLASS_RX_TIMEOUT;
  }
  if (ret == -ENOTCONN) {
    return (uint8_t)MAPEK_MCPS_CLASS_ENOTCONN;
  }
  if (ret == -EBUSY || ret == -EAGAIN) {
    return (uint8_t)MAPEK_MCPS_CLASS_BUSY;
  }
  if (ret == -EINVAL) {
    return (uint8_t)MAPEK_MCPS_CLASS_INVAL;
  }
  return (uint8_t)MAPEK_MCPS_CLASS_OTHER;
}

static void ewma_u8_q8(int32_t *state_q8, uint8_t sample) {
  const unsigned int sh = (unsigned int)MAPEK_LINK_EWMA_SHIFT;
  int32_t x = (int32_t)sample << 8;
  if (*state_q8 < 0) {
    *state_q8 = x;
  } else {
    *state_q8 += (x - *state_q8) >> sh;
  }
}

/** Signed sample EWMA; @a state_q8 is INT32_MIN until first sample (not -1: RSSI Q8 is negative). */
static void ewma_i16_q8(int32_t *state_q8, int16_t sample) {
  const unsigned int sh = (unsigned int)MAPEK_LINK_EWMA_SHIFT;
  int32_t x = (int32_t)sample << 8;
  if (*state_q8 == INT32_MIN) {
    *state_q8 = x;
  } else {
    *state_q8 += (x - *state_q8) >> sh;
  }
}

static void ewma_i8_q8(int32_t *state_q8, int8_t sample) {
  ewma_i16_q8(state_q8, (int16_t)sample);
}

static void snapshot_fill_locked(mapek_link_monitor_snapshot_t *out) {
  out->joined = mon_joined;
  out->join_transition_uptime_ms = mon_join_transition_ms;

  out->linkcheck_have_sample = lc_have;
  out->last_margin_db = lc_last_margin;
  out->last_nb_gateways = lc_last_nb_gw;
  out->last_linkcheck_uptime_ms = lc_last_uptime_ms;
  out->ewma_margin_db =
      (ewma_margin_q8 >= 0) ? (uint8_t)((uint32_t)ewma_margin_q8 >> 8U) : 0U;
  out->ewma_nb_gateways =
      (ewma_nb_gw_q8 >= 0) ? (uint8_t)((uint32_t)ewma_nb_gw_q8 >> 8U) : 0U;

  out->dl_have_sample = dl_have;
  out->last_dl_rssi = dl_last_rssi;
  out->last_dl_snr = dl_last_snr;
  out->last_dl_uptime_ms = dl_last_uptime_ms;
  out->last_dl_app_payload = dl_last_app;
  out->last_dl_lorawan_time_updated = dl_last_time_updated;
  out->ewma_dl_rssi =
      (ewma_rssi_q8 != INT32_MIN) ? (int16_t)(ewma_rssi_q8 >> 8) : (int16_t)0;
  out->ewma_dl_snr =
      (ewma_snr_q8 != INT32_MIN) ? (int8_t)(ewma_snr_q8 >> 8) : (int8_t)0;

  out->mcps_last_errno = mcps_last_ret;
  out->mcps_last_class = mcps_last_class;
  out->mcps_last_ul_kind = mcps_last_kind;
  out->mcps_last_port = mcps_last_port;
  out->mcps_last_len = mcps_last_len;
  out->mcps_last_confirmed = mcps_last_confirmed;
  out->mcps_last_uptime_ms = mcps_last_uptime_ms;
  out->mcps_app_ok_count = mcps_cnt_app_ok;
  out->mcps_app_fail_count = mcps_cnt_app_fail;
  out->mcps_lc_tx_ok_count = mcps_cnt_lc_ok;
  out->mcps_lc_tx_fail_count = mcps_cnt_lc_fail;
  out->linkcheck_req_pending = lc_req_pending;
  out->linkcheck_req_sent_uptime_ms = lc_req_sent_ms;
}

static void monitor_reset_locked(void) {
  ewma_margin_q8 = -1;
  ewma_nb_gw_q8 = -1;
  ewma_rssi_q8 = INT32_MIN;
  ewma_snr_q8 = INT32_MIN;

  mon_joined = false;
  mon_join_transition_ms = 0U;

  lc_have = false;
  lc_last_margin = 0U;
  lc_last_nb_gw = 0U;
  lc_last_uptime_ms = 0U;

  dl_have = false;
  dl_last_rssi = 0;
  dl_last_snr = 0;
  dl_last_uptime_ms = 0U;
  dl_last_app = false;
  dl_last_time_updated = false;

  mcps_last_ret = 0;
  mcps_last_class = (uint8_t)MAPEK_MCPS_CLASS_OK;
  mcps_last_kind = (uint8_t)MAPEK_UL_MCPS_APP;
  mcps_last_port = 0U;
  mcps_last_len = 0U;
  mcps_last_confirmed = false;
  mcps_last_uptime_ms = 0U;
  mcps_cnt_app_ok = 0U;
  mcps_cnt_app_fail = 0U;
  mcps_cnt_lc_ok = 0U;
  mcps_cnt_lc_fail = 0U;
  lc_req_pending = false;
  lc_req_sent_ms = 0U;
}

static void monitor_log_timer_handler(struct k_timer *timer);
static void monitor_log_work_handler(struct k_work *work);

K_TIMER_DEFINE(mapek_monitor_log_timer, monitor_log_timer_handler, NULL);
K_WORK_DEFINE(mapek_monitor_log_work, monitor_log_work_handler);

static void monitor_log_timer_handler(struct k_timer *timer) {
  ARG_UNUSED(timer);
  (void)k_work_submit(&mapek_monitor_log_work);
}

static void monitor_log_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  k_mutex_lock(&mon_mutex, K_FOREVER);
  monitor_log_inf_locked();
  k_mutex_unlock(&mon_mutex);
}

static void monitor_log_inf_locked(void) {
  mapek_link_monitor_snapshot_t s;
  snapshot_fill_locked(&s);

  LOG_INF(
      "mapek Mon: joined=%u lc(last=%u gw=%u ewma_m=%u ewma_g=%u) "
      "dl(rssi=%d snr=%d app=%u tu=%u ewma_r=%d ewma_s=%d) "
      "mcps(kind=%u p=%u ret=%d cls=%u cfm=%u lc_pend=%u "
      "cnt_app_ok=%u cnt_app_fail=%u cnt_lc_ok=%u cnt_lc_fail=%u)",
      (unsigned)s.joined, (unsigned)s.last_margin_db,
      (unsigned)s.last_nb_gateways, (unsigned)s.ewma_margin_db,
      (unsigned)s.ewma_nb_gateways, (int)s.last_dl_rssi, (int)s.last_dl_snr,
      (unsigned)s.last_dl_app_payload,
      (unsigned)s.last_dl_lorawan_time_updated, (int)s.ewma_dl_rssi,
      (int)s.ewma_dl_snr, (unsigned)s.mcps_last_ul_kind,
      (unsigned)s.mcps_last_port, (int)s.mcps_last_errno,
      (unsigned)s.mcps_last_class, (unsigned)s.mcps_last_confirmed,
      (unsigned)s.linkcheck_req_pending, (unsigned)s.mcps_app_ok_count,
      (unsigned)s.mcps_app_fail_count, (unsigned)s.mcps_lc_tx_ok_count,
      (unsigned)s.mcps_lc_tx_fail_count);
}

void mapek_link_init(void) {
  k_mutex_lock(&mon_mutex, K_FOREVER);
  monitor_reset_locked();
  k_mutex_unlock(&mon_mutex);

  if (MAPEK_LINK_MONITOR_LOG_INTERVAL_MS > 0U) {
    k_timer_start(&mapek_monitor_log_timer,
                  K_MSEC(MAPEK_LINK_MONITOR_LOG_INTERVAL_MS),
                  K_MSEC(MAPEK_LINK_MONITOR_LOG_INTERVAL_MS));
  }

  LOG_INF("mapek_link Monitor init (EWMA shift=%u log_period_ms=%u)",
          (unsigned)MAPEK_LINK_EWMA_SHIFT,
          (unsigned)MAPEK_LINK_MONITOR_LOG_INTERVAL_MS);
}

void mapek_link_step(void) {
  /* Reserved for Analyze/Plan/Execute; periodic INF uses timer work item. */
}

void mapek_link_feed_link_check(uint8_t demod_margin_db, uint8_t nb_gateways) {
  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    LOG_WRN("mapek feed LC dropped (lock)");
    return;
  }

  lc_have = true;
  lc_last_margin = demod_margin_db;
  lc_last_nb_gw = nb_gateways;
  lc_last_uptime_ms = k_uptime_get_32();

  lc_req_pending = false;

  ewma_u8_q8(&ewma_margin_q8, demod_margin_db);
  ewma_u8_q8(&ewma_nb_gw_q8, nb_gateways);

  LOG_INF(
      "mapek feed LC margin=%u gw=%u ewma_m=%u ewma_g=%u",
      (unsigned)demod_margin_db, (unsigned)nb_gateways,
      (unsigned)((ewma_margin_q8 >= 0) ? ((uint32_t)ewma_margin_q8 >> 8U)
                                        : 0U),
      (unsigned)((ewma_nb_gw_q8 >= 0) ? ((uint32_t)ewma_nb_gw_q8 >> 8U)
                                       : 0U));

  k_mutex_unlock(&mon_mutex);
}

void mapek_link_feed_mcps_uplink(int mcps_ret, uint8_t ul_kind, uint8_t port,
                                 uint8_t len, bool confirmed) {
  k_mutex_lock(&mon_mutex, K_FOREVER);

  const uint8_t cls = classify_mcps_ret(mcps_ret);

  mcps_last_ret = (int16_t)mcps_ret;
  mcps_last_class = cls;
  mcps_last_kind = ul_kind;
  mcps_last_port = port;
  mcps_last_len = len;
  mcps_last_confirmed = confirmed;
  mcps_last_uptime_ms = k_uptime_get_32();

  if (ul_kind == (uint8_t)MAPEK_UL_MCPS_APP) {
    if (mcps_ret == 0) {
      if (mcps_cnt_app_ok < UINT32_MAX) {
        mcps_cnt_app_ok++;
      }
    } else {
      if (mcps_cnt_app_fail < UINT32_MAX) {
        mcps_cnt_app_fail++;
      }
    }
  } else if (ul_kind == (uint8_t)MAPEK_UL_MCPS_LINK_CHECK) {
    if (mcps_ret == 0) {
      if (mcps_cnt_lc_ok < UINT32_MAX) {
        mcps_cnt_lc_ok++;
      }
      lc_req_pending = true;
      lc_req_sent_ms = mcps_last_uptime_ms;
    } else {
      if (mcps_cnt_lc_fail < UINT32_MAX) {
        mcps_cnt_lc_fail++;
      }
    }
  }

  LOG_INF(
      "mapek feed MCPS kind=%u port=%u len=%u cfm=%u ret=%d class=%u",
      (unsigned)ul_kind, (unsigned)port, (unsigned)len,
      confirmed ? 1U : 0U, mcps_ret, (unsigned)cls);

  k_mutex_unlock(&mon_mutex);
}

void mapek_link_feed_dl(int16_t rssi, int8_t snr, uint8_t feed_flags) {
  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    LOG_WRN("mapek feed DL dropped (lock)");
    return;
  }

  const bool app = (feed_flags & MAPEK_DL_FEED_APP_PAYLOAD) != 0U;
  const bool tu = (feed_flags & MAPEK_DL_FEED_LORAWAN_TIME_UPD) != 0U;

  dl_have = true;
  dl_last_rssi = rssi;
  dl_last_snr = snr;
  dl_last_uptime_ms = k_uptime_get_32();
  dl_last_app = app;
  dl_last_time_updated = tu;

  ewma_i16_q8(&ewma_rssi_q8, rssi);
  ewma_i8_q8(&ewma_snr_q8, snr);

  LOG_INF("mapek feed DL rssi=%d snr=%d app=%u tu=%u ewma_r=%d ewma_s=%d",
          (int)rssi, (int)snr, (unsigned)app, (unsigned)tu,
          (ewma_rssi_q8 != INT32_MIN) ? (int)(ewma_rssi_q8 >> 8) : 0,
          (ewma_snr_q8 != INT32_MIN) ? (int)(ewma_snr_q8 >> 8) : 0);

  k_mutex_unlock(&mon_mutex);
}

void mapek_link_feed_join(bool joined) {
  k_mutex_lock(&mon_mutex, K_FOREVER);

  if (mon_joined != joined) {
    mon_joined = joined;
    mon_join_transition_ms = k_uptime_get_32();
    LOG_INF("mapek feed join -> %u", (unsigned)joined);
  }

  if (!joined) {
    lc_have = false;
    dl_have = false;
    ewma_margin_q8 = -1;
    ewma_nb_gw_q8 = -1;
    ewma_rssi_q8 = INT32_MIN;
    ewma_snr_q8 = INT32_MIN;
    lc_req_pending = false;
    lc_req_sent_ms = 0U;
    LOG_INF("mapek join cleared: LC/DL samples + EWMA reset");
  }

  k_mutex_unlock(&mon_mutex);
}

bool mapek_link_monitor_get(mapek_link_monitor_snapshot_t *out) {
  if (out == NULL) {
    return false;
  }

  k_mutex_lock(&mon_mutex, K_FOREVER);
  snapshot_fill_locked(out);
  k_mutex_unlock(&mon_mutex);
  return true;
}
