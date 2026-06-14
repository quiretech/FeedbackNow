/**
 * Counter-sync uplinks: one EVT_COUNTER_SYNC (0x12) per button on DL 0x04 only.
 */
#ifndef COUNTER_SYNC_H
#define COUNTER_SYNC_H

#include <stdbool.h>
#include <stdint.h>

/** @return Number of counter-sync uplinks successfully queued. */
uint32_t counter_sync_run(uint32_t epoch_s, bool confirmed);

#endif /* COUNTER_SYNC_H */
