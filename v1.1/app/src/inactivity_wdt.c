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