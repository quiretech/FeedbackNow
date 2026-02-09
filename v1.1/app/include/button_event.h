/**
 * High-level button events published by the Input (button) subsystem on zbus.
 * Subscribers: system mode, application logic.
 */
#ifndef BUTTON_EVENT_H
#define BUTTON_EVENT_H

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

/* Declare so channel can list this observer (defined in main.c or elsewhere) */
ZBUS_LISTENER_DECLARE(button_event_listener);

/* High-level event kinds: single button tap or combo (after hold duration) */
typedef enum {
    BUTTON_EVENT_KIND_SINGLE_0,
    BUTTON_EVENT_KIND_SINGLE_1,
    BUTTON_EVENT_KIND_SINGLE_2,
    BUTTON_EVENT_KIND_SINGLE_3,
    BUTTON_EVENT_KIND_SINGLE_4,
    BUTTON_EVENT_KIND_SINGLE_5,
    BUTTON_EVENT_KIND_COMBO_STAFF_MODE,
    BUTTON_EVENT_KIND_COMBO_DEVICE_INFO,
    BUTTON_EVENT_KIND_COMBO_DELIBERATE_JOIN,
    BUTTON_EVENT_KIND_COMBO_REBOOT,
    BUTTON_EVENT_KIND_COUNT
} button_event_kind_t;

typedef struct {
    button_event_kind_t kind;
    int64_t timestamp_ms;
} button_event_msg_t;

/* zbus channel name for button_event (define in .c with ZBUS_CHAN_DECLARE) */
#define BUTTON_EVENT_CHAN_NAME "button_event"

#endif /* BUTTON_EVENT_H */
