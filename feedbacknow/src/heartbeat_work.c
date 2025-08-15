#include "lora_app.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(heartbeat, CONFIG_LOG_DEFAULT_LEVEL);

#define HEARTBEAT_INTERVAL_MS 60000 // 60 seconds

static struct k_work heartbeat_work;
static struct k_timer heartbeat_timer;

static void heartbeat_work_handler(struct k_work *work) {
  lora_uplink_msg_t hb_msg = {0};
  hb_msg.port = 10;
  hb_msg.len = 3;
  hb_msg.data[0] = 0xAA;
  hb_msg.data[1] = 0xBB;
  hb_msg.data[2] = 0xCC;

  hb_msg.confirmed = false;

  int ret = lora_put_event(&hb_msg, K_NO_WAIT);
  if (ret == 0) {
    LOG_INF("Heartbeat enqueued, len=%d", hb_msg.len);
  } else {
    LOG_ERR("Failed to enqueue heartbeat: %d", ret);
  }
}

static void heartbeat_timer_handler(struct k_timer *timer) {
  k_work_submit(&heartbeat_work);
}

void heartbeat_init(void) {
  k_work_init(&heartbeat_work, heartbeat_work_handler);
  k_timer_init(&heartbeat_timer, heartbeat_timer_handler, NULL);
  k_timer_start(&heartbeat_timer, K_MSEC(HEARTBEAT_INTERVAL_MS),
                K_MSEC(HEARTBEAT_INTERVAL_MS));
}
