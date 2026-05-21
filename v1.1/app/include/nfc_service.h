/**
 * NFC service: PN5180 ISO15693 scan for Staff check-in/out/vote (FLEXBOX_PLUS only).
 * When NFC_ENABLED=0 (DEVICE_HW_VARIANT FLEXBOX), all APIs are no-ops.
 * SMF requests scan via nfc_scan_start(); worker posts SMF_EVT_NFC_RESULT.
 */
#ifndef NFC_SERVICE_H
#define NFC_SERVICE_H

#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Intent for this scan (maps to payload event type). */
enum nfc_intent {
  NFC_INTENT_CHECK_IN = 0x01,  /* EVT_NFC_IN */
  NFC_INTENT_CHECK_OUT = 0x02, /* EVT_NFC_OUT */
  NFC_INTENT_VOTE = 0x03,      /* EVT_NFC_VOTE */
};

/** Thread ID for main to start (K_THREAD_DEFINE with delay = -1). */
extern const k_tid_t nfc_worker_id;

/**
 * One-time init: PN5180 init and configure ISO15693. Call after rails/GPIO.
 * Start nfc_worker_id from main thread block.
 * @return 0 on success, negative on error.
 */
int nfc_service_init(void);
int nfc_service_wait_until_ready(k_timeout_t timeout);

/**
 * Start NFC scan. Worker will run until card read or timeout/cancel.
 * @param intent CHECK_IN, CHECK_OUT, or NFC_VOTE
 * @param button_id 0..5 (check-in=0, check-out=1, vote=2..5)
 */
void nfc_scan_start(uint8_t intent, uint8_t button_id);

/**
 * Request worker to cancel current scan (e.g. on SMF NFC timeout).
 */
void nfc_scan_cancel(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_SERVICE_H */
