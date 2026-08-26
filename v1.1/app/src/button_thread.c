/**
 * Input layer: press/release from buttons.c, combo holds, single-tap votes → SMF.
 * Combo/recovery deadlines use k_work_delayable; thread blocks on the button queue.
 */
#include "button_thread.h"
#include "buttons.h"
#include "log_fmt.h"
#include "smf_system_mode.h"
#include "sys_config.h"

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input, CONFIG_LOG_DEFAULT_LEVEL);
K_SEM_DEFINE(button_thread_ready_sem, 0, 1);

#define BUTTON_MASK(b) (1U << (b))
#define BUTTON_MASK_ALL ((1U << NUM_BUTTONS) - 1U)

typedef struct {
  uint32_t mask;
  uint32_t hold_ms;
  uint8_t ev_type;
} combo_def_t;

// Combo table: mask, hold_ms, event type. Order matters: first match wins.
// This table is used to for combo event creation
// The button combinations and their corresponding hold times and event types are defined here
static const combo_def_t combo_table[] = {
    {BUTTON_MASK_ALL, COMBO_FACTORY_RESET_HOLD_MS,SMF_EVT_COMBO_FACTORY_RESET},
    {BUTTON_MASK(0) | BUTTON_MASK(1) | BUTTON_MASK(2) | BUTTON_MASK(3), COMBO_REBOOT_HOLD_MS, SMF_EVT_COMBO_REBOOT},
    {BUTTON_MASK(0) | BUTTON_MASK(1) | BUTTON_MASK(5), COMBO_DEVICE_INFO_HOLD_MS, SMF_EVT_COMBO_DEVICE_INFO},
    {BUTTON_MASK(0) | BUTTON_MASK(1) | BUTTON_MASK(2), COMBO_JOIN_HOLD_MS, SMF_EVT_COMBO_JOIN},
    {BUTTON_MASK(0) | BUTTON_MASK(1), COMBO_STAFF_HOLD_MS, SMF_EVT_COMBO_STAFF},
};

struct input_state {
  uint32_t held;
  uint32_t session_buttons;
  bool session_combo_fired;
  const combo_def_t *combo_timed;
  int64_t combo_start_ms;
  uint32_t last_fired_combo_mask;
  int64_t held_zero_at_ms;
};

static struct input_state st;
static K_MUTEX_DEFINE(input_mtx);
static void input_deadline_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(input_deadline_work, input_deadline_work_handler);

static const combo_def_t *combo_lookup(uint32_t held) {
  for (size_t i = 0; i < ARRAY_SIZE(combo_table); i++) {
    if (combo_table[i].mask == held) {
      return &combo_table[i];
    }
  }
  return NULL;
}

/** Exactly one bit set → button index; else -1. */
static int session_lone_button(uint32_t session_buttons) {
  if (session_buttons == 0U ||
      (session_buttons & (session_buttons - 1U)) != 0U) {
    return -1;
  }
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (session_buttons & BUTTON_MASK(i)) {
      return i;
    }
  }
  return -1;
}

static void input_reset_session_locked(void) {
  st.session_buttons = 0;
  st.session_combo_fired = false;
  st.last_fired_combo_mask = 0;
  st.held_zero_at_ms = 0;
  st.combo_timed = NULL;
}

static void input_fire_combo_locked(const combo_def_t *combo) {
  LOG_STATE("combo fire ev=%u mask=0x%x hold_ms=%u",
          (unsigned)combo->ev_type, (unsigned)combo->mask,
          (unsigned)combo->hold_ms);
  (void)smf_post_event(combo->ev_type, 0, k_uptime_get());
  st.session_combo_fired = true;
  st.last_fired_combo_mask = combo->mask;
  st.combo_timed = NULL;
}

static void input_sync_combo_timer_locked(void) {
  const combo_def_t *match = combo_lookup(st.held);
  if (match != NULL && st.held != st.last_fired_combo_mask) {
    if (st.combo_timed != match) {
      st.combo_timed = match;
      st.combo_start_ms = k_uptime_get();
      LOG_DBG("combo arm ev=%u mask=0x%x hold_ms=%u t=%lld",
              (unsigned)match->ev_type, (unsigned)match->mask,
              (unsigned)match->hold_ms, (long long)st.combo_start_ms);
    }
  } else {
    if (st.combo_timed != NULL) {
      LOG_DBG("combo disarm held=0x%x", (unsigned)st.held);
    }
    st.combo_timed = NULL;
  }
}

/** Caller must hold input_mtx. Runs combo fire and/or session recovery. */
static void input_process_deadlines_locked(int64_t now) {
  if (st.combo_timed != NULL && st.held == st.combo_timed->mask &&
      now >= st.combo_start_ms + (int64_t)st.combo_timed->hold_ms) {
    input_fire_combo_locked(st.combo_timed);
  }

  if (st.held != 0 || st.session_buttons == 0 || st.held_zero_at_ms == 0) {
    return;
  }

  if ((now - st.held_zero_at_ms) < (int64_t)INPUT_SESSION_RECOVERY_MS) {
    return;
  }

  st.held = buttons_get_held_mask();
  if (st.held != 0) {
    LOG_DBG("recovery abort: gpio held=0x%x (session=0x%x)",
            (unsigned)st.held, (unsigned)st.session_buttons);
    st.held_zero_at_ms = 0;
    input_sync_combo_timer_locked();
    return;
  }

  LOG_DBG("session recovery session=0x%x after %u ms idle",
          (unsigned)st.session_buttons, (unsigned)INPUT_SESSION_RECOVERY_MS);
  input_reset_session_locked();
}

/** Caller must hold input_mtx. */
static void input_reschedule_deadline_locked(void) {
  const int64_t now = k_uptime_get();
  int64_t next = INT64_MAX;

  if (st.combo_timed != NULL && st.held == st.combo_timed->mask &&
      st.held != st.last_fired_combo_mask) {
    int64_t t = st.combo_start_ms + (int64_t)st.combo_timed->hold_ms;
    next = (t > now) ? t : now;
  }

  if (st.held == 0 && st.session_buttons != 0 && st.held_zero_at_ms > 0) {
    int64_t t = st.held_zero_at_ms + (int64_t)INPUT_SESSION_RECOVERY_MS;
    if (t < next) {
      next = (t > now) ? t : now;
    }
  }

  if (next == INT64_MAX) {
    if (k_work_delayable_is_pending(&input_deadline_work)) {
      LOG_DBG("deadline cancel (no pending combo/recovery)");
    }
    (void)k_work_cancel_delayable(&input_deadline_work);
    return;
  }

  uint32_t delay_ms = (uint32_t)(next - now);
  if (delay_ms == 0U) {
    delay_ms = 1U;
  }

  const bool combo_pending =
      st.combo_timed != NULL && st.held == st.combo_timed->mask &&
      st.held != st.last_fired_combo_mask;
  const bool recovery_pending =
      st.held == 0 && st.session_buttons != 0 && st.held_zero_at_ms > 0;

  LOG_DBG("deadline schedule %u ms combo=%d recovery=%d held=0x%x session=0x%x",
          (unsigned)delay_ms, (int)combo_pending, (int)recovery_pending,
          (unsigned)st.held, (unsigned)st.session_buttons);
  (void)k_work_reschedule(&input_deadline_work, K_MSEC(delay_ms));
}

static void input_deadline_work_handler(struct k_work *work) {
  ARG_UNUSED(work);

  LOG_DBG("deadline work fired (delayable, not poll)");
  k_mutex_lock(&input_mtx, K_FOREVER);
  input_process_deadlines_locked(k_uptime_get());
  input_reschedule_deadline_locked();
  k_mutex_unlock(&input_mtx);
}

static void input_handle_event_locked(const button_event_t *evt) {
  if (evt->type == BUTTON_EVENT_PRESS) {
    st.held |= BUTTON_MASK(evt->button_id);
    st.session_buttons |= BUTTON_MASK(evt->button_id);
  } else {
    st.held &= ~BUTTON_MASK(evt->button_id);
  }

  LOG_DBG("btn%u %s held=0x%x session=0x%x",
          (unsigned)evt->button_id,
          evt->type == BUTTON_EVENT_PRESS ? "press" : "release",
          (unsigned)st.held, (unsigned)st.session_buttons);

  if (st.held == 0) {
    if (st.held_zero_at_ms == 0) {
      st.held_zero_at_ms = k_uptime_get();
    }
  } else {
    st.held_zero_at_ms = 0;
  }

  input_sync_combo_timer_locked();

  if (evt->type != BUTTON_EVENT_RELEASE || st.held != 0 ||
      st.session_buttons == 0) {
    return;
  }

  if (!st.session_combo_fired) {
    const int btn = session_lone_button(st.session_buttons);
    if (btn >= 0) {
      LOG_STATE("single tap btn=%d -> SMF (session end)", btn);
      (void)smf_post_event(SMF_EVT_BUTTON_SINGLE_0 + (uint8_t)btn, (uint8_t)btn,
                           evt->timestamp_ms);
    } else {
      LOG_DBG("release session=0x%x no single (combo_fired=%d)",
              (unsigned)st.session_buttons, (int)st.session_combo_fired);
    }
  }

  input_reset_session_locked();
}

static void button_input_thread_fn(void *a, void *b, void *c) {
  button_event_t evt;

  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  k_sem_give(&button_thread_ready_sem);
  LOG_STATE("input thread ready");

  while (1) {
    (void)buttons_get_event(&evt, K_FOREVER);

    k_mutex_lock(&input_mtx, K_FOREVER);
    if (evt.button_id < NUM_BUTTONS) {
      input_handle_event_locked(&evt);
    }
    input_reschedule_deadline_locked();
    k_mutex_unlock(&input_mtx);
  }
}

K_THREAD_DEFINE(button_uplink_thread_id, BUTTON_THREAD_STACK_SIZE,
                button_input_thread_fn, NULL, NULL, NULL,
                BUTTON_THREAD_PRIORITY, 0, -1);

int button_thread_wait_until_ready(k_timeout_t timeout) {
  return k_sem_take(&button_thread_ready_sem, timeout);
}
