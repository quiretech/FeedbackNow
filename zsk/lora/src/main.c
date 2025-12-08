/*
 * Class A LoRaWAN sample application
 *
 * Copyright (c) 2020 Manivannan Sadhasivam <mani@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>

#include "lorawan_keys.h"
#include "nvs.h"
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#define DELAY K_MSEC(10000)
#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL

// #define NVS_LORAWAN_KEYS

LOG_MODULE_REGISTER(main);

char data[] = {'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd'};

static void dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr,
                        uint8_t len, const uint8_t *hex_data) {
  LOG_INF("Port %d, Pending %d, RSSI %ddB, SNR %ddBm, Time %d", port,
          flags & LORAWAN_DATA_PENDING, rssi, snr,
          !!(flags & LORAWAN_TIME_UPDATED));
  if (hex_data) {
    LOG_HEXDUMP_INF(hex_data, len, "Payload: ");
  }
}

static void lorwan_datarate_changed(enum lorawan_datarate dr) {
  uint8_t unused, max_size;

  lorawan_get_payload_sizes(&unused, &max_size);
  LOG_INF("New Datarate: DR_%d, Max Payload %d", dr, max_size);
}

int main(void) {
  int ret;
  uint16_t dev_nonce = 0;
  ssize_t bytes_written;

#ifdef NVS_LORAWAN_KEYS
  uint8_t dev_eui[8];
  uint8_t join_eui[8];
  uint8_t app_key[16];

#else
  uint8_t dev_eui[] = LORAWAN_DEV_EUI;
  uint8_t join_eui[] = LORAWAN_JOIN_EUI;
  uint8_t app_key[] = LORAWAN_APP_KEY;
#endif

  /*Init Lorawan Device & Functions*/

  const struct device *lora_dev;
  struct lorawan_join_config join_cfg;
  struct lorawan_downlink_cb downlink_cb = {.port = LW_RECV_PORT_ANY,
                                            .cb = dl_callback};
  lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
  if (!device_is_ready(lora_dev)) {
    LOG_ERR("%s: device not ready.", lora_dev->name);
    return 0;
  } else {
    LOG_ERR("%s: device ready.", lora_dev->name);
  }

#if defined(CONFIG_LORAMAC_REGION_EU868)
  /* If more than one region Kconfig is selected, app should set region
   * before calling lorawan_start()
   */
  ret = lorawan_set_region(LORAWAN_REGION_EU868);
  if (ret < 0) {
    LOG_ERR("lorawan_set_region failed: %d", ret);
    return 0;
  }
#endif

  ret = lorawan_start();
  if (ret < 0) {
    LOG_ERR("lorawan_start failed: %d", ret);
    return 0;
  }

  lorawan_register_downlink_callback(&downlink_cb);
  lorawan_register_dr_changed_callback(lorwan_datarate_changed);

  /*Init NVS flash to get Keys Stored on NVS*/
  static struct nvs_fs fs;
  nvs_initialize(&fs);
  nvs_read_init_parameter(&fs, NVS_DEVNONCE_ID, &dev_nonce);

#ifdef NVS_LORAWAN_KEYS
  nvs_read_init_parameter(&fs, NVS_LORAWAN_DEV_EUI_ID, dev_eui);
  nvs_read_init_parameter(&fs, NVS_LORAWAN_JOIN_EUI_ID, join_eui);
  nvs_read_init_parameter(&fs, NVS_LORAWAN_APP_KEY_ID, app_key);
#endif

  join_cfg.mode = LORAWAN_ACT_OTAA;
  join_cfg.dev_eui = dev_eui;
  join_cfg.otaa.join_eui = join_eui;
  join_cfg.otaa.app_key = app_key;
  join_cfg.otaa.nwk_key = app_key;
  join_cfg.otaa.dev_nonce = dev_nonce;

  int i = 1;

  do {
    LOG_INF("Joining network using OTAA, devNonce: %d; attempt: %d",
            join_cfg.otaa.dev_nonce, i++);
    ret = lorawan_join(&join_cfg);
    if (ret < 0) {
      if ((ret = -ETIMEDOUT)) {
        LOG_WRN("Timed-out waiting for response.");
      } else {
        LOG_ERR("Join failed (%d)", ret);
      }
    } else {
      LOG_INF("Join successful.");
    }
    dev_nonce++;
    join_cfg.otaa.dev_nonce = dev_nonce;

    // Save value away in Non-Volatile Storage.
    bytes_written =
        nvs_write(&fs, NVS_DEVNONCE_ID, &dev_nonce, sizeof(dev_nonce));
    if (bytes_written < 0) {
      LOG_ERR("NVS: Failed to write id %d (%d)", NVS_DEVNONCE_ID,
              bytes_written);
    } else {
      // LOG_INF("NVS: Wrote %d bytes to id %d",bytes_written,
      // NVS_DEVNONCE_ID);
    }

    if (ret < 0) {
      // If failed, wait before re-trying.
      k_sleep(K_MSEC(5000));
    }

  } while (ret != 0);

  LOG_INF("Sending data...");
  while (1) {
    ret = lorawan_send(2, data, sizeof(data), LORAWAN_MSG_CONFIRMED);

    /*
     * Note: The stack may return -EAGAIN if the provided data
     * length exceeds the maximum possible one for the region and
     * datarate. But since we are just sending the same data here,
     * we'll just continue.
     */
    if (ret == -EAGAIN) {
      LOG_ERR("lorawan_send failed: %d. Continuing...", ret);
      k_sleep(DELAY);
      continue;
    }

    if (ret < 0) {
      LOG_ERR("lorawan_send failed: %d", ret);
      return 0;
    }

    LOG_INF("Data sent!");
    k_sleep(DELAY);
  }
}
