/**
 * LoRaWAN application downlink (FRMPayload) dispatch — port/payload only.
 * SMF passes ops for actions that must stay in the FSM (e.g. reboot timer).
 * Application cmd byte 0: 0x01–0x06 existing; 0x07 = reboot (SMF: LED then cold
 * reboot, same as Staff combo reboot); 0x08 = query firmware/hardware version
 * (EVT 0x14 on FPORT_DEVICE_INFO 21 only). EVT 0x13 (tz + last_cleaned) is sent on
 * FPORT_HOUSEKEEPING 20 from post-join, daily HK, and DL 0x04 — see
 * downlink_queue_housekeeping_state_snapshot().
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
