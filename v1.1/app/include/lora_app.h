#ifndef LORA_APP_H
#define LORA_APP_H

#include "eui_keys.h"
#include "sys_config.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/sys/atomic.h>

/**
 * @brief Initialize LoRaWAN stack
 */
int lora_app_init(void);

/**
 * @brief LoRaWAN application downlink (FRMPayload) callback from the stack.
 * @param frmpayload Downlink application payload bytes (length @a len).
 */
void lora_app_dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr,
                          uint8_t len, const uint8_t *frmpayload);

/**
 * @brief Data rate change callback
 */
void lora_app_dr_changed(enum lorawan_datarate dr);

// LoRa configuration is now in sys_config.h

/**
 * LoRa command types: SMF (or bootstrap) posts these to the LoRa command queue.
 * Only the LoRa thread consumes them; it never posts commands to itself except
 * to bootstrap auto-join (has_joined_once). SMF must not call lorawan_* APIs.
 */
enum lora_cmd_type {
  LORA_CMD_JOIN = 0,   /* Deliberate join (Staff+0+1+2): show Connecting on EPD */
  LORA_CMD_JOIN_SILENT, /* Auto-join / rejoin: no EPD; LED only first silent after
                         * boot, not after link backoff */
  LORA_CMD_TIME_SYNC,   /* Request DeviceTimeReq/Ans and update RTC */
  LORA_CMD_TIME_SYNC_RETRY, /* Retry DeviceTimeReq in active sync cycle */
  LORA_CMD_LINK_CHECK,       /* Append LinkCheckReq to next uplink */
  LORA_CMD_LINK_CHECK_FORCE, /* Send empty frame now for LinkCheckReq */
  LORA_CMD_ENABLE_ADR,       /* lorawan_enable_adr(true); idempotent */
  LORA_CMD_COUNT
};

typedef struct {
  uint8_t port;
  uint8_t len;
  bool confirmed;
  uint8_t data[LORA_MAX_PAYLOAD_SIZE]; // Moved to end for better alignment
} lora_uplink_msg_t;

/** Which uplink burst just finished enqueueing — selects DeviceTime defer. */
enum lora_burst_tail_profile {
  /** Post-join: NUM_BUTTONS counter UL + housekeeping snapshot UL. */

  LORA_BURST_TAIL_JOIN_POST,
  /** Housekeeping: HK + counters + snapshot. */

  LORA_BURST_TAIL_HOUSEKEEPING,
};

typedef struct {
  uint8_t button_id; // Unique button identifier
} button_payload_t;

extern struct k_msgq lora_msgq;
extern struct k_msgq lora_cmdq;
extern struct k_mutex lora_send_mutex;

// LoRa thread ID
extern const k_tid_t lora_thread_id;

/* Join status tracking - atomic flag for thread safety */
extern atomic_t lora_joined_flag;

/* Semaphore to signal join completion to waiting threads */
extern struct k_sem lora_join_sem;
int lora_wait_until_ready(k_timeout_t timeout);

/**
 * Request LoRa thread to perform OTAA join. SMF uses this (e.g. on COMBO_JOIN).
 * On first boot LoRa thread blocks until this is called; on later boots
 * the thread auto-posts LORA_CMD_JOIN. Join retry logic stays inside LoRa
 * thread.
 */
void lora_request_join(void);

/**
 * Request LoRa thread to run time sync (DeviceTimeReq/Ans, update RTC).
 * SMF can call this for housekeeping; LoRa thread owns all lorawan_* calls.
 */
void lora_request_time_sync(void);

/**
 * After counter-sync (+ HK/snapshot burst) enqueue, wait for approximate drain
 * then queue DeviceTimeReq. Cancels any prior deferral before scheduling.
 */
void lora_schedule_time_sync_after_counter_burst(
    enum lora_burst_tail_profile profile);

/** Cancel deferral queued by lora_schedule_time_sync_after_counter_burst. */

void lora_cancel_scheduled_burst_time_sync(void);

/**
 * Request LoRa thread to re-enable ADR. Idempotent — ADR is already enabled
 * after successful join; remains so after DeviceTime completes.
 */
void lora_request_enable_adr(void);

/**
 * Request LoRa thread to send LinkCheckReq MAC command. LoRa thread calls
 * lorawan_request_link_check(force_request). For heartbeat use true to send
 * immediately; false appends to next uplink.
 */
void lora_request_link_check(bool force_request);

/**
 * Reset one-shot DR-based time sync retry guard. Call on each new join cycle
 * transition so DR callback can request time sync once per join.
 */
void lora_reset_dr_time_sync_retry(void);

/**
 * Notify LoRa app layer that a new join succeeded.
 * Caller schedules DeviceTimeReq after the counter burst deferral elsewhere.
 */

void lora_on_join_success(void);

/** Post a command to the LoRa thread (SMF or LoRa thread for self-bootstrap
 * only). */
int lora_cmd_put(uint8_t cmd);
/** Get next command from LoRa command queue (LoRa thread only). Returns true if
 * a command was read. */
bool lora_get_cmd(uint8_t *cmd_out, k_timeout_t timeout);

/**
 * @brief Check if device has successfully joined the LoRaWAN network
 * @return true if joined, false otherwise
 */
static inline bool lora_is_joined(void) {
  return atomic_get(&lora_joined_flag) != 0;
}

/* Message queue API */
bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout);
int lora_put_event(const lora_uplink_msg_t *msg, k_timeout_t timeout);

#endif // LORA_APP_H
