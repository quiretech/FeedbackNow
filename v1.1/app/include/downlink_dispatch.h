/**
 * LoRaWAN application downlink (FRMPayload) dispatch — port/payload only.
 * SMF passes ops for actions that must stay in the FSM (e.g. reboot timer).
 * Single-byte cmd (payload[0]): 0x01–0x08; 0x99 custom EPD text ([1]=dur min, [2…]=hex pairs).
 * EVT 0x14 on FPORT_DEVICE_INFO 21 after 0x08 (no leading ts in FRMPayload). EVT 0x13 on FPORT 20 — see
 * downlink_queue_housekeeping_state_snapshot().
 *
 * 0x99: dur 0 uses DL_CUSTOM_TEXT_DEFAULT_MINUTES. Hex body = pairs of ASCII
 * '0'–'9'/'A'–'F'/'a'–'f' → printable message on EPD; then revert to LAST_CLEANED/CLEANING.
 */
#ifndef DOWNLINK_DISPATCH_H
#define DOWNLINK_DISPATCH_H

#include <stdint.h>

struct downlink_dispatch_ops {
  /** After factory reset: LED pattern then schedule cold reboot after delay. */
  void (*schedule_reboot_led_ms)(uint32_t led_hold_ms);
};

void downlink_dispatch(uint8_t port, uint8_t len, const uint8_t *frmpayload,
                       const struct downlink_dispatch_ops *ops);

/** Queue EVT_DEVICE_STATE_SNAPSHOT (0x13) on FPORT_HOUSEKEEPING when joined. */
void downlink_queue_housekeeping_state_snapshot(void);

#endif /* DOWNLINK_DISPATCH_H */
