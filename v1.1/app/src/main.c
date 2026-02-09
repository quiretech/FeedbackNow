/**
 * FlexBox v1.2 — main. Inits button subsystem; button thread runs independently
 * and publishes high-level events on zbus (button_event channel).
 */
#include "buttons.h"
#include "button_event.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static void button_event_listener(const struct zbus_channel *chan)
{
    const button_event_msg_t *msg = zbus_chan_msg(chan);
    LOG_INF("button_event: kind=%d ts=%lld", (int)msg->kind, msg->timestamp_ms);
}

ZBUS_LISTENER_DEFINE(button_event_listener, button_event_listener);

int main(void)
{
    int ret = buttons_init();
    if (ret != 0) {
        LOG_ERR("buttons_init failed: %d", ret);
        return ret;
    }
    LOG_INF("Button subsystem ready; thread running");

    while (1) {
        k_sleep(K_FOREVER);
    }
    return 0;
}
