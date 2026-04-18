#include "counter_sync.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "rtc.h"
#include "sys_config.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(counter_sync, CONFIG_LOG_DEFAULT_LEVEL);

void counter_sync_run(bool confirmed) {
  uint32_t epoch_s = 0;

  (void)rtc_get_epoch_seconds(&epoch_s);
  if (epoch_s == 0) {
    epoch_s = (uint32_t)(k_uptime_get() / 1000U);
  }

  LOG_DBG("[counter_sync] evt 0x%02X per button (confirmed=%d)",
          (unsigned)EVT_COUNTER_SYNC, (int)confirmed);

  for (uint8_t btn = 0; btn < NUM_BUTTONS; btn++) {
    uint8_t payload[PAYLOAD_LEN_BYTES];
    int ret = payload_gen_build_counter_sync(btn, epoch_s, payload);
    if (ret != 0) {
      LOG_WRN("[counter_sync] btn=%u build failed: %d", btn, ret);
      continue;
    }
    lora_uplink_msg_t msg = {0};
    msg.port = FPORT_HOUSEKEEPING;
    msg.confirmed = confirmed;
    msg.len = PAYLOAD_LEN_BYTES;
    memcpy(msg.data, payload, PAYLOAD_LEN_BYTES);
    ret = lora_put_event(&msg, K_MSEC(500));
    if (ret == 0) {
      LOG_INF("[counter_sync] queued button %u", btn);
    } else {
      LOG_WRN("[counter_sync] lora_put_event btn=%u failed: %d", btn, ret);
    }
#if COUNTER_SYNC_DELAY_MS > 0
    k_msleep(COUNTER_SYNC_DELAY_MS);
#endif
  }

  LOG_DBG("[counter_sync] done");
}
