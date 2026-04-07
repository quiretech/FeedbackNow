#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#include "sys_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>

// NFC event types
typedef enum {
  NFC_EVENT_SCAN_START,
  NFC_EVENT_TAG_DETECTED,
  NFC_EVENT_SCAN_TIMEOUT,
  NFC_EVENT_SCAN_STOP
} nfc_event_type_t;

// NFC event structure
typedef struct {
  nfc_event_type_t type;
  uint8_t uid[NFC_UID_LENGTH];
  int64_t timestamp_ms;
} nfc_event_t;

// NFC manager functions
int nfc_manager_init(void);
int nfc_manager_start_scan(void);
int nfc_manager_stop_scan(void);
bool nfc_manager_is_scanning(void);
int nfc_manager_trigger_scan(void);

// NFC event queue
extern struct k_msgq nfc_event_queue;

// NFC manager thread ID
extern const k_tid_t nfc_manager_thread_id;

#endif // NFC_MANAGER_H
