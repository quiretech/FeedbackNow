/**
 * Application logic (per architecture): map mode + button + NFC to actions.
 * Public vote: cooldown, LED, increment counter, build payload, request uplink.
 */
#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stdint.h>

/**
 * Called from system mode FSM when in Normal and a single button press event
 * is received. Enforces 5s cooldown, LED feedback, payload build, uplink.
 * @param button_id  Driver button index (0..NUM_BUTTONS-1), used for payload.
 */
void app_logic_public_vote(uint8_t button_id);

#endif /* APP_LOGIC_H */
