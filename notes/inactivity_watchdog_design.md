# Inactivity Watchdog Engine Design Specification (24-Hour Auto-Restart)

## 1. Overview & Objective
The goal is to restart the FeedbackNow unit if no button interaction occurs within a continuous 24-hour window (86,400 seconds).

### Key Architectural Distinctions
* **Hardware Watchdog (WDT)**: A hardware microcontroller peripheral (e.g. nRF52 WDT) running at 32.768 kHz. Hardware counter limits restrict maximum timeout (often ~131 seconds max on nRF52). Its role is crash/hang/deadlock recovery, not day-long business logic.
* **Inactivity Watchdog Engine (Software)**: Application-layer timer using Zephyr's `k_work_delayable` on the system workqueue. It implements the 24-hour window while maintaining ultra-low-power tickless sleep (< 10 µA idle).

---

## 2. Configuration & Tunables

In `sys_config.h` and `sys_config_profile.h`:

```c
#if SYS_CONFIG_LAB_FAST
  /* 60 seconds for bench/lab testing */
  #define INACTIVITY_WDT_TIMEOUT_SEC 60U
#else
  /* 24 hours for field deployment */
  #define INACTIVITY_WDT_TIMEOUT_SEC (24U * 3600U)
#endif
```

---

## 3. Module Implementation

### Header: `app/include/inactivity_wdt.h`

```c
#ifndef INACTIVITY_WDT_H
#define INACTIVITY_WDT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize and schedule the initial 24-hour inactivity watchdog timer. */
void inactivity_wdt_init(void);

/** Reset/feed the 24-hour timer upon user button interaction. */
void inactivity_wdt_feed(void);

/** Get elapsed seconds since the last registered button press. */
uint32_t inactivity_wdt_get_elapsed_sec(void);

#ifdef __cplusplus
}
#endif

#endif /* INACTIVITY_WDT_H */
```

### Source: `app/src/inactivity_wdt.c`

```c
#include "inactivity_wdt.h"
#include "sys_config.h"
#include "log_fmt.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#if defined(CONFIG_SOC_FAMILY_NRF)
#include <hal/nrf_power.h>
#endif

LOG_MODULE_REGISTER(inactivity_wdt, CONFIG_LOG_DEFAULT_LEVEL);

#define GPREGRET_MAGIC_INACTIVITY_REBOOT 0x49 /* 'I' */

static int64_t last_activity_uptime_ms;
static void inactivity_timeout_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(inactivity_work, inactivity_timeout_handler);

static void inactivity_timeout_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    LOG_WRN("Inactivity watchdog: no button pressed for %u s -> restarting",
            (unsigned)INACTIVITY_WDT_TIMEOUT_SEC);

#if defined(CONFIG_SOC_FAMILY_NRF)
    /* Retain reboot reason in retention register so boot_info can identify it */
    nrf_power_gpregret_set(NRF_POWER, GPREGRET_MAGIC_INACTIVITY_REBOOT);
#endif

    /* Allow deferred log ring buffer to flush before reboot */
    k_msleep(50);

    sys_reboot(SYS_REBOOT_COLD);
}

void inactivity_wdt_init(void)
{
    last_activity_uptime_ms = k_uptime_get();
    LOG_INF("Inactivity WDT initialized (timeout = %u s)",
            (unsigned)INACTIVITY_WDT_TIMEOUT_SEC);

    (void)k_work_schedule(&inactivity_work, K_SECONDS(INACTIVITY_WDT_TIMEOUT_SEC));
}

void inactivity_wdt_feed(void)
{
    last_activity_uptime_ms = k_uptime_get();
    (void)k_work_reschedule(&inactivity_work, K_SECONDS(INACTIVITY_WDT_TIMEOUT_SEC));
    LOG_DBG("Inactivity WDT fed (next timeout in %u s)",
            (unsigned)INACTIVITY_WDT_TIMEOUT_SEC);
}

uint32_t inactivity_wdt_get_elapsed_sec(void)
{
    int64_t elapsed_ms = k_uptime_get() - last_activity_uptime_ms;
    return (elapsed_ms < 0) ? 0 : (uint32_t)(elapsed_ms / 1000);
}
```

---

## 4. Integration Points

### 4.1 Button Thread (`app/src/button_thread.c`)
Hook into `input_handle_event_locked`:
```c
#include "inactivity_wdt.h"

static void input_handle_event_locked(const button_event_t *evt) {
    if (evt->type == BUTTON_EVENT_PRESS) {
        inactivity_wdt_feed();
        st.held |= BUTTON_MASK(evt->button_id);
        st.session_buttons |= BUTTON_MASK(evt->button_id);
    }
    ...
}
```

### 4.2 System Initialization (`app/src/main.c`)
Inside `system_init()`:
```c
#include "inactivity_wdt.h"

static int system_init(void) {
    ...
    inactivity_wdt_init();
    ...
}
```

### 4.3 Commissioning Boot Shielding (`app/src/boot_info.c`)
In `boot_info.c`, check whether `NRF_POWER->GPREGRET == GPREGRET_MAGIC_INACTIVITY_REBOOT`.
If matched, clear the register and flag the boot as non-commissioning so the unit restores the normal operational display rather than triggering the installer/commissioning screen.

---

## 5. Build Configuration
Add the source file to `app/CMakeLists.txt`:
```cmake
target_sources(app PRIVATE
    src/inactivity_wdt.c
)
```
