/**
 * Counter-sync uplinks: one EVT_COUNTER_SYNC per button after join / HK.
 */
#ifndef COUNTER_SYNC_H
#define COUNTER_SYNC_H

#include <stdbool.h>

void counter_sync_run(bool confirmed);

#endif /* COUNTER_SYNC_H */
