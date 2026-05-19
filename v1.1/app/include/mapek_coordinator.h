/**
 * MAPE-K coordinator — public API (feeds + uplink gate).
 */
#ifndef MAPEK_COORDINATOR_H
#define MAPEK_COORDINATOR_H

#include "mapek/mapek_types.h"

#include <stdbool.h>
#include <stdint.h>

#define MAPEK_DL_FEED_APP_PAYLOAD ((uint8_t)(1u << 0))
#define MAPEK_DL_FEED_LORAWAN_TIME_UPD ((uint8_t)(1u << 1))

#include "mapek/mapek_mon.h"

void mapek_init(const uint8_t dev_eui[8]);
void mapek_start(void);

bool mapek_uplink_allowed(void);
link_state_t mapek_link_state_get(void);

#endif /* MAPEK_COORDINATOR_H */
