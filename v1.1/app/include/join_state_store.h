/**
 * Persistent "has joined once" flag in EEPROM.
 * First boot: has_joined_once = 0 → device does not auto-join; user must
 * trigger deliberate join (Staff + 0+1+2). After first successful join we set
 * the flag so subsequent boots auto-join.
 */
#ifndef JOIN_STATE_STORE_H
#define JOIN_STATE_STORE_H

#include <stdbool.h>

/** Initialize store (load from EEPROM). Call once before get/set. */
int join_state_store_init(void);

/** Get has_joined_once. Returns 0 and sets *out; -EINVAL if out is NULL. */
int join_state_store_has_joined_once(bool *out);

/** Set has_joined_once = true and persist to EEPROM. */
int join_state_store_set_has_joined_once(void);

/** Clear has_joined_once = false and persist to EEPROM (e.g. factory reset via downlink 0x06). */
int join_state_store_clear_has_joined_once(void);

#endif /* JOIN_STATE_STORE_H */
