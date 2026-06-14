#include "counter_sync.h"
#include "eui_keys.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "sys_config.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(counter_sync, CONFIG_LOG_DEFAULT_LEVEL);

/** Milliseconds before each counter-sync enqueue: base delay + jitter. */
static uint32_t counter_sync_spacing_ms(uint8_t btn_idx) {
  uint32_t base = (uint32_t)COUNTER_SYNC_DELAY_MS;
#if COUNTER_SYNC_JITTER_MAX_MS > 0
  uint32_t mix = (uint32_t)btn_idx * 0x9e3779b9U;
  /* Copy DevEUI from macro (compound literal shape from eui_keys.h). */
  const uint8_t de[] = LORAWAN_DEV_EUI;
  for (size_t i = 0; i < sizeof(de); i++) {
    mix = (mix * 31U) + (uint32_t)de[i];
  }
  mix ^= (uint32_t)(k_uptime_get() >> 3);

  base += mix % ((uint32_t)COUNTER_SYNC_JITTER_MAX_MS + 1U);

#endif /* COUNTER_SYNC_JITTER_MAX_MS > 0 */
  return base;
}

uint32_t counter_sync_run(uint32_t epoch_s, bool confirmed) {
  uint32_t queued = 0U;

  LOG_DBG("evt 0x%02X per button (confirmed=%d)",
          (unsigned)EVT_COUNTER_SYNC, (int)confirmed);

  for (uint8_t btn = 0; btn < NUM_BUTTONS; btn++) {
    uint32_t spacing = counter_sync_spacing_ms(btn);
    if (spacing > 0U) {
      k_msleep((int32_t)spacing);
    }
    uint8_t payload[PAYLOAD_LEN_BYTES];
    int ret = payload_gen_build_counter_sync(btn, epoch_s, payload);
    if (ret != 0) {
      LOG_WRN("btn=%u build failed: %d", btn, ret);
      continue;
    }
    lora_uplink_msg_t msg = {0};
    msg.port = FPORT_HOUSEKEEPING;
    msg.confirmed = confirmed;
    msg.len = PAYLOAD_LEN_BYTES;
    memcpy(msg.data, payload, PAYLOAD_LEN_BYTES);
    ret = lora_put_event(&msg, K_MSEC(500));
    if (ret == 0) {
      queued++;
      LOG_DBG("queued btn%u", btn);
    } else {
      LOG_WRN("lora_put_event btn=%u failed: %d", btn, ret);
    }
  }

  LOG_DBG("done queued=%u", (unsigned)queued);
  return queued;
}
