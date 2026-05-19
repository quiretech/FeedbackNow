#ifndef MAPEK_MON_H
#define MAPEK_MON_H

#include "mapek/mapek_types.h"

#include <stdbool.h>
#include <stdint.h>

void mapek_mon_init(void);
void mapek_feed_join(bool joined);
void mapek_feed_dl(int16_t rssi, int8_t snr, uint8_t feed_flags);
void mapek_feed_link_check_ans(uint8_t margin_db, uint8_t nb_gw);
void mapek_feed_heartbeat_tx(int tx_ret);
void mapek_feed_heartbeat_result(heartbeat_result_t result);
void mapek_mon_snapshot(mon_snap_t *out);

#endif /* MAPEK_MON_H */
