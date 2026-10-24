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