/**
 * System mode FSM (per architecture Layer 4).
 * Single SMF thread blocks on one input queue; events: button_combo, timeouts,
 * nfc_result, joined, downlink (latter added in later phases).
 */
#ifndef SMF_SYSTEM_MODE_H
#define SMF_SYSTEM_MODE_H

#include <stdint.h>
#include <zephyr/kernel.h>

/* Event types posted to SMF input queue */
enum smf_ev_type {
  SMF_EVT_NONE = 0,
  /* Button: single press (payload = button_id 0..NUM_BUTTONS-1) */
  SMF_EVT_BUTTON_SINGLE_0,
  SMF_EVT_BUTTON_SINGLE_1,
  SMF_EVT_BUTTON_SINGLE_2,
  SMF_EVT_BUTTON_SINGLE_3,
  SMF_EVT_BUTTON_SINGLE_4,
  SMF_EVT_BUTTON_SINGLE_5,
  /* Combo events (from Input layer when hold duration reached) */
  SMF_EVT_COMBO_STAFF,       /* 0+1 hold 2s */
  SMF_EVT_COMBO_DEVICE_INFO, /* 0+1+5 hold 3s */
  SMF_EVT_COMBO_JOIN,        /* 0+1+2 hold 3s */
  SMF_EVT_COMBO_REBOOT,      /* 0+1+2+3 hold 10s */
  /* Timeouts (Phase 2+) */
  SMF_EVT_STAFF_TIMEOUT,
  SMF_EVT_NFC_TIMEOUT,
  SMF_EVT_DEVICE_INFO_TIMEOUT,
  /* LoRa / NFC (Phase 2+) */
  SMF_EVT_JOINED,
  SMF_EVT_JOIN_STARTED,   /* LoRa thread started join (orchestration visibility)
                           */
  SMF_EVT_TIME_SYNC_DONE, /* LoRa thread finished time sync (button_id: 0=ok,
                             1=fail) */
  SMF_EVT_DISCONNECTED,
  SMF_EVT_DOWNLINK,
  SMF_EVT_NFC_RESULT,
  SMF_EVT_HOUSEKEEPING_TICK, /* Periodic housekeeping (time sync, later link
                                check, battery) */
  SMF_EVT_COUNT
};

/* One message in the SMF input queue */
typedef struct smf_msg {
  uint8_t ev_type;
  uint8_t button_id;
  int64_t timestamp_ms;
  /* Optional payload for nfc_result, downlink, etc. (Phase 2+) */
  union {
    uint32_t user_id;
    uint8_t port;
    struct {
      uint8_t port;
      uint8_t len;
    } downlink;
    struct {
      uint8_t ok;        /* 1 = read success, 0 = timeout/error */
      uint8_t intent;    /* nfc_intent_t: CHECK_IN, CHECK_OUT, NFC_VOTE */
      uint8_t button_id; /* 0..5 for vote / check-in(0) / check-out(1) */
      uint8_t pad;
    } nfc;
  } payload;
} smf_msg_t;

/**
 * Post an event to the SMF input queue (e.g. from Input thread).
 * For button single: use SMF_EVT_BUTTON_SINGLE_0 + button_id, and set
 * button_id.
 */
int smf_post_event(uint8_t ev_type, uint8_t button_id, int64_t timestamp_ms);

/**
 * Post downlink to SMF (from LoRa downlink callback). Copies payload into
 * SMF-owned buffer; len must be <= LORA_MAX_PAYLOAD_SIZE.
 */
int smf_post_downlink(uint8_t port, uint8_t len, const uint8_t *data);

/**
 * Post NFC scan result to SMF. Copies data_4 into SMF-owned buffer.
 * ok=1 success, ok=0 timeout/error; intent = check-in/check-out/vote;
 * button_id = 0..5 (for vote); data_4 = 4 bytes from tag block.
 */
int smf_post_nfc_result(uint8_t ok, uint8_t intent, uint8_t button_id,
                        const uint8_t *data_4);

/**
 * Start the SMF thread (or use K_THREAD_DEFINE and start from main).
 * Thread ID for main to start.
 */
extern const k_tid_t smf_thread_id;

#endif /* SMF_SYSTEM_MODE_H */
