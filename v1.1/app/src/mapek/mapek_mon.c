#include "mapek/mapek_mon.h"

#include "mapek/mapek_config.h"

#include <stddef.h>
#include <zephyr/kernel.h>

#define EWMA_UNINIT INT32_MIN

static K_MUTEX_DEFINE(mon_mutex);

static int32_t ewma_rssi_q8 = EWMA_UNINIT;
static int32_t ewma_snr_q8 = EWMA_UNINIT;

static bool mon_joined;
static uint32_t last_lns_heard_ms;

static bool dl_rf_have;
static heartbeat_result_t hb_result = HB_NOT_FIRED;
static uint32_t hb_last_ms;
static uint8_t hb_lc_margin;
static uint8_t hb_lc_nb_gw;

static void ewma_i16_q8(int32_t *state_q8, int16_t sample) {
  const unsigned int sh = (unsigned int)MAPEK_LINK_EWMA_SHIFT;
  const int32_t x = (int32_t)sample << 8;
  if (*state_q8 == EWMA_UNINIT) {
    *state_q8 = x;
  } else {
    *state_q8 += (x - *state_q8) >> sh;
  }
}

static void ewma_i8_q8(int32_t *state_q8, int8_t sample) {
  ewma_i16_q8(state_q8, (int16_t)sample);
}

static void mon_mark_lns_heard(void) {
  last_lns_heard_ms = k_uptime_get_32();
}

void mapek_mon_init(void) {
  k_mutex_lock(&mon_mutex, K_FOREVER);
  ewma_rssi_q8 = EWMA_UNINIT;
  ewma_snr_q8 = EWMA_UNINIT;
  mon_joined = false;
  last_lns_heard_ms = 0U;
  dl_rf_have = false;
  hb_result = HB_NOT_FIRED;
  hb_last_ms = 0U;
  k_mutex_unlock(&mon_mutex);
}

void mapek_feed_join(bool joined) {
  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    return;
  }
  if (mon_joined != joined) {
    mon_joined = joined;
    if (!joined) {
      ewma_rssi_q8 = EWMA_UNINIT;
      ewma_snr_q8 = EWMA_UNINIT;
      last_lns_heard_ms = 0U;
      dl_rf_have = false;
      hb_result = HB_NOT_FIRED;
      hb_last_ms = 0U;
    }
  }
  k_mutex_unlock(&mon_mutex);
}

void mapek_feed_dl(int16_t rssi, int8_t snr, uint8_t feed_flags) {
  ARG_UNUSED(feed_flags);

  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    return;
  }
  dl_rf_have = true;
  mon_mark_lns_heard();
  ewma_i16_q8(&ewma_rssi_q8, rssi);
  ewma_i8_q8(&ewma_snr_q8, snr);
  if (hb_result == HB_PENDING) {
    hb_result = HB_SUCCESS;
    hb_last_ms = k_uptime_get_32();
  }
  k_mutex_unlock(&mon_mutex);
}

void mapek_feed_link_check_ans(uint8_t margin_db, uint8_t nb_gw) {
  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    return;
  }
  hb_lc_margin = margin_db;
  hb_lc_nb_gw = nb_gw;
  mon_mark_lns_heard();
  if (hb_result == HB_PENDING || hb_result == HB_NOT_FIRED) {
    hb_result = HB_SUCCESS;
    hb_last_ms = k_uptime_get_32();
  }
  k_mutex_unlock(&mon_mutex);
}

void mapek_feed_heartbeat_tx(int tx_ret) {
  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    return;
  }
  if (tx_ret == 0) {
    hb_result = HB_PENDING;
  } else {
    hb_result = HB_TX_FAIL;
    hb_last_ms = k_uptime_get_32();
  }
  k_mutex_unlock(&mon_mutex);
}

void mapek_feed_heartbeat_result(heartbeat_result_t result) {
  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    return;
  }
  if (result == HB_NO_ANSWER || result == HB_SUCCESS || result == HB_TX_FAIL) {
    hb_result = result;
    hb_last_ms = k_uptime_get_32();
  }
  k_mutex_unlock(&mon_mutex);
}

void mapek_mon_snapshot(mon_snap_t *out) {
  if (out == NULL) {
    return;
  }
  k_mutex_lock(&mon_mutex, K_FOREVER);
  const uint32_t now = k_uptime_get_32();
  out->joined = mon_joined;
  out->lns_ever_heard = (last_lns_heard_ms != 0U);
  out->lns_heard_age_ms =
      out->lns_ever_heard ? (now - last_lns_heard_ms) : 0U;
  out->dl_rf_valid =
      dl_rf_have && ewma_rssi_q8 != EWMA_UNINIT && ewma_snr_q8 != EWMA_UNINIT;
  out->ewma_rssi =
      (ewma_rssi_q8 != EWMA_UNINIT) ? (int16_t)(ewma_rssi_q8 >> 8) : 0;
  out->ewma_snr =
      (ewma_snr_q8 != EWMA_UNINIT) ? (int8_t)(ewma_snr_q8 >> 8) : 0;
  out->hb_result = hb_result;
  out->hb_last_ms = hb_last_ms;
  out->hb_lc_margin_db = hb_lc_margin;
  out->hb_lc_nb_gw = hb_lc_nb_gw;
  out->now_ms = now;
  k_mutex_unlock(&mon_mutex);
}
