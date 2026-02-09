/**
 * Input subsystem: consumes raw debounced press/release from buttons.c,
 * tracks held set, runs combo hold timers, publishes high-level events on zbus.
 */
#include "button_event.h"
#include "buttons.h"
#include "sys_config.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(button_thread, LOG_LEVEL_INF);

#define BUTTON_MASK(b) (1U << (b))
#define HELD_SET_0_1      (BUTTON_MASK(0) | BUTTON_MASK(1))
#define HELD_SET_0_1_2    (HELD_SET_0_1 | BUTTON_MASK(2))
#define HELD_SET_0_1_2_3  (HELD_SET_0_1_2 | BUTTON_MASK(3))
#define HELD_SET_0_1_5    (HELD_SET_0_1 | BUTTON_MASK(5))

ZBUS_CHAN_DEFINE(button_event, button_event_msg_t, NULL, NULL,
                 ZBUS_OBSERVERS(button_event_listener));

static void publish_event(button_event_kind_t kind)
{
    button_event_msg_t msg = {
        .kind = kind,
        .timestamp_ms = k_uptime_get(),
    };
    int ret = zbus_chan_pub(ZBUS_CHAN_GET(button_event), &msg, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("zbus pub failed: %d", ret);
    } else {
        LOG_INF("Published %d", (int)kind);
    }
}

static uint32_t combo_hold_ms(uint32_t held)
{
    if (held == HELD_SET_0_1_2_3) return COMBO_REBOOT_HOLD_MS;
    if (held == HELD_SET_0_1_2)   return COMBO_JOIN_HOLD_MS;
    if (held == HELD_SET_0_1_5)   return COMBO_DEVICE_INFO_HOLD_MS;
    if (held == HELD_SET_0_1)     return COMBO_STAFF_HOLD_MS;
    return 0;
}

static button_event_kind_t combo_kind(uint32_t held)
{
    if (held == HELD_SET_0_1_2_3) return BUTTON_EVENT_KIND_COMBO_REBOOT;
    if (held == HELD_SET_0_1_2)   return BUTTON_EVENT_KIND_COMBO_DELIBERATE_JOIN;
    if (held == HELD_SET_0_1_5)   return BUTTON_EVENT_KIND_COMBO_DEVICE_INFO;
    if (held == HELD_SET_0_1)     return BUTTON_EVENT_KIND_COMBO_STAFF_MODE;
    return BUTTON_EVENT_KIND_COUNT;
}

static int popcount(uint32_t x)
{
    int n = 0;
    for (; x; x &= x - 1) n++;
    return n;
}

static button_event_kind_t single_kind_for_button(uint8_t id)
{
    if (id <= 5) return (button_event_kind_t)(BUTTON_EVENT_KIND_SINGLE_0 + id);
    return BUTTON_EVENT_KIND_COUNT;
}

void button_thread_func(void *a, void *b, void *c)
{
    button_event_t raw;
    uint32_t held = 0;           /* currently held (debounced) */
    uint32_t session_buttons = 0; /* union of all held since last all-release */
    bool session_combo_fired = false;
    uint32_t active_combo = 0;   /* which combo we're timing (0 if none) */
    int64_t combo_start_ms = 0;

    LOG_INF("Button thread started");

    while (1) {
        if (!buttons_get_event(&raw, K_MSEC(50))) {
            /* Timeout: check combo hold expiry */
            if (active_combo != 0 && held == active_combo) {
                uint32_t need_ms = combo_hold_ms(active_combo);
                if (k_uptime_get() - combo_start_ms >= need_ms) {
                    button_event_kind_t k = combo_kind(active_combo);
                    publish_event(k);
                    session_combo_fired = true;
                    active_combo = 0;
                }
            }
            continue;
        }

        if (raw.type == BUTTON_EVENT_PRESS) {
            held |= BUTTON_MASK(raw.button_id);
            session_buttons |= BUTTON_MASK(raw.button_id);

            uint32_t need_ms = combo_hold_ms(held);
            if (need_ms != 0) {
                if (active_combo != held) {
                    active_combo = held;
                    combo_start_ms = raw.timestamp_ms;
                }
            } else {
                active_combo = 0;
            }
        } else {
            /* release */
            held &= ~BUTTON_MASK(raw.button_id);

            if (held == 0) {
                /* all released: emit single-button if exactly one was in session
                 * and we didn't fire a combo */
                if (!session_combo_fired && popcount(session_buttons) == 1) {
                    for (int i = 0; i < NUM_BUTTONS; i++) {
                        if (session_buttons & BUTTON_MASK(i)) {
                            publish_event(single_kind_for_button((uint8_t)i));
                            break;
                        }
                    }
                }
                /* reset session for next time */
                session_buttons = 0;
                session_combo_fired = false;
                active_combo = 0;
            } else {
                /* still holding some: re-evaluate combo */
                uint32_t need_ms = combo_hold_ms(held);
                if (need_ms != 0) {
                    active_combo = held;
                    combo_start_ms = raw.timestamp_ms;
                } else {
                    active_combo = 0;
                }
            }
        }
    }
}

K_THREAD_DEFINE(button_thread_id, BUTTON_THREAD_STACK_SIZE, button_thread_func,
                NULL, NULL, NULL, BUTTON_THREAD_PRIORITY, 0, 0);
