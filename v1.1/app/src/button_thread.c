/**
 * Input layer: consumes raw press/release from buttons.c, tracks held set,
 * detects combos via configurable table, posts single-button or combo events to
 * SMF.
 *
 * To extend: add a row to combo_table[] and (if needed) a new SMF_EVT_COMBO_*
 * in smf_system_mode.h; add hold time to sys_config.h.
 */
#include "button_thread.h"
#include "buttons.h"
#include "smf_system_mode.h"
#include "sys_config.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(input, CONFIG_LOG_DEFAULT_LEVEL);

#define BUTTON_MASK(b) (1U << (b))
#define COMBO_SCAN_INTERVAL_MS 50

/* Combo definition: mask of buttons that must be held, hold time (ms), SMF
 * event */
typedef struct {
  uint32_t mask;
  uint32_t hold_ms;
  uint8_t ev_type;
} combo_def_t;

/* Order: more specific (more buttons) first so longest match wins */
static const combo_def_t combo_table[] = {
    {BUTTON_MASK(0) | BUTTON_MASK(1) | BUTTON_MASK(2) | BUTTON_MASK(3),
     COMBO_REBOOT_HOLD_MS, SMF_EVT_COMBO_REBOOT},
    {BUTTON_MASK(0) | BUTTON_MASK(1) | BUTTON_MASK(5),
     COMBO_DEVICE_INFO_HOLD_MS, SMF_EVT_COMBO_DEVICE_INFO},
    {BUTTON_MASK(0) | BUTTON_MASK(1) | BUTTON_MASK(2), COMBO_JOIN_HOLD_MS,
     SMF_EVT_COMBO_JOIN},
    {BUTTON_MASK(0) | BUTTON_MASK(1), COMBO_STAFF_HOLD_MS, SMF_EVT_COMBO_STAFF},
};
#define NUM_COMBOS ((int)(sizeof(combo_table) / sizeof(combo_table[0])))

static int popcount(uint32_t x) {
  int n = 0;
  for (; x; x &= x - 1)
    n++;
  return n;
}

static const combo_def_t *combo_lookup(uint32_t held) {
  for (int i = 0; i < NUM_COMBOS; i++) {
    if (combo_table[i].mask == held) {
      return &combo_table[i];
    }
  }
  return NULL;
}

static void button_input_thread_fn(void *a, void *b, void *c) {
  button_event_t btn_evt;
  uint32_t held = 0;
  uint32_t session_buttons = 0;
  bool session_combo_fired = false;
  const combo_def_t *combo_being_timed = NULL;
  int64_t combo_start_ms = 0;
  uint32_t last_fired_combo_mask =
      0; /* avoid re-firing same combo while held */

  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  LOG_INF("Input thread started (single + combo -> SMF)");

  while (1) {
    bool got = buttons_get_event(&btn_evt, K_MSEC(COMBO_SCAN_INTERVAL_MS));

    if (got && btn_evt.button_id < NUM_BUTTONS) {
      if (btn_evt.type == BUTTON_EVENT_PRESS) {
        held |= BUTTON_MASK(btn_evt.button_id);
        session_buttons |= BUTTON_MASK(btn_evt.button_id);
      } else {
        held &= ~BUTTON_MASK(btn_evt.button_id);
      }
    }

    /* Update combo timing: if held matches a combo, start/continue; else cancel
     */
    const combo_def_t *match = combo_lookup(held);
    if (match != NULL && held != last_fired_combo_mask) {
      if (combo_being_timed != match) {
        combo_being_timed = match;
        combo_start_ms = k_uptime_get();
      }
    } else {
      combo_being_timed = NULL;
    }

    /* Check if combo hold duration reached */
    if (combo_being_timed != NULL && held == combo_being_timed->mask) {
      int64_t elapsed = k_uptime_get() - combo_start_ms;
      if (elapsed >= (int64_t)combo_being_timed->hold_ms) {
        LOG_INF("[Input] combo fired: ev=%u", combo_being_timed->ev_type);
        (void)smf_post_event(combo_being_timed->ev_type, 0, k_uptime_get());
        session_combo_fired = true;
        last_fired_combo_mask =
            combo_being_timed->mask; /* prevent re-fire while held */
        combo_being_timed = NULL;
      }
    }

    /* On all released: emit single-button if exactly one in session and no
     * combo */
    if (got && btn_evt.type == BUTTON_EVENT_RELEASE && held == 0) {
      if (!session_combo_fired && popcount(session_buttons) == 1) {
        for (int i = 0; i < NUM_BUTTONS; i++) {
          if (session_buttons & BUTTON_MASK(i)) {
            uint8_t ev_type = SMF_EVT_BUTTON_SINGLE_0 + (uint8_t)i;
            LOG_INF("[Input] single button %d -> SMF", i);
            (void)smf_post_event(ev_type, (uint8_t)i, btn_evt.timestamp_ms);
            break;
          }
        }
      }
      session_buttons = 0;
      session_combo_fired = false;
      last_fired_combo_mask = 0;
    }
  }
}

K_THREAD_DEFINE(button_uplink_thread_id, BUTTON_THREAD_STACK_SIZE,
                button_input_thread_fn, NULL, NULL, NULL,
                BUTTON_THREAD_PRIORITY, 0, -1);
