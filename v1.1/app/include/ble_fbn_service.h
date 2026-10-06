/**
 * @file ble_fbn_service.h
 * @brief FeedbackNow Custom Bluetooth LE GATT Service.
 *
 * Exposes:
 *  1. Configuration Characteristic (Read active parameters; Write new LoRa keys,
 *     Unit ID, or sync RTC time).
 *  2. Diagnostics Characteristic (Read / Notify live telemetry: Battery mV, RTC time,
 *     LoRa join status, link margin, gateway count, button counters 1-5, last cleaned).
 *  3. Control Characteristic (Write action commands: force join, test uplink,
 *     clear counters, refresh display, reboot, stop BLE).
 */

#ifndef BLE_FBN_SERVICE_H
#define BLE_FBN_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FeedbackNow Custom 128-bit UUIDs
 * Base: 46424e00-0001-4000-8000-00805f9b34fb ('FBN')
 */
#define BT_UUID_FBN_VAL \
  BT_UUID_128_ENCODE(0x46424e00, 0x0001, 0x4000, 0x8000, 0x00805f9b34fb)

#define BT_UUID_FBN_CONFIG_VAL \
  BT_UUID_128_ENCODE(0x46424e01, 0x0001, 0x4000, 0x8000, 0x00805f9b34fb)

#define BT_UUID_FBN_DIAG_VAL \
  BT_UUID_128_ENCODE(0x46424e02, 0x0001, 0x4000, 0x8000, 0x00805f9b34fb)

#define BT_UUID_FBN_CTRL_VAL \
  BT_UUID_128_ENCODE(0x46424e03, 0x0001, 0x4000, 0x8000, 0x00805f9b34fb)

#define BT_UUID_FBN_SERVICE      BT_UUID_DECLARE_128(BT_UUID_FBN_VAL)
#define BT_UUID_FBN_CONFIG_CHRC  BT_UUID_DECLARE_128(BT_UUID_FBN_CONFIG_VAL)
#define BT_UUID_FBN_DIAG_CHRC    BT_UUID_DECLARE_128(BT_UUID_FBN_DIAG_VAL)
#define BT_UUID_FBN_CTRL_CHRC    BT_UUID_DECLARE_128(BT_UUID_FBN_CTRL_VAL)

/* Configuration Command OpCodes (written to Config Characteristic) */
enum fbn_ble_config_cmd {
  FBN_CFG_CMD_SET_LORA_KEYS = 0x01,
  FBN_CFG_CMD_SET_IDENTITY  = 0x02,
  FBN_CFG_CMD_SYNC_RTC      = 0x03,
  FBN_CFG_CMD_RESET_CONFIG  = 0x04,
};

/* Control Action OpCodes (written to Control Characteristic) */
enum fbn_ble_ctrl_cmd {
  FBN_CTRL_CMD_LORA_JOIN        = 0x01,
  FBN_CTRL_CMD_LORA_TEST_UPLINK = 0x02,
  FBN_CTRL_CMD_CLEAR_COUNTERS   = 0x03,
  FBN_CTRL_CMD_REFRESH_DISPLAY  = 0x04,
  FBN_CTRL_CMD_REBOOT           = 0x05,
  FBN_CTRL_CMD_STOP_BLE         = 0x06,
};

/**
 * Packed binary payload returned when Reading the Configuration Characteristic
 */
struct __packed fbn_ble_config_read_resp {
  uint8_t  magic;             /* 0xFB */
  uint8_t  version;           /* 0x01 */
  uint8_t  is_provisioned;    /* 1 if EEPROM provisioned, 0 if compile-time */
  uint8_t  lora_region;       /* 1=US915, 2=EU868, 3=AU915, 4=AS923 */
  uint8_t  dev_eui[8];        /* LoRaWAN DevEUI */
  uint8_t  join_eui[8];       /* LoRaWAN JoinEUI / AppEUI */
  uint8_t  app_key[16];       /* LoRaWAN AppKey */
  char     unit_id[16];       /* Unit identifier string */
  char     client_name[24];   /* Client / Deployment tag */
  uint32_t provision_epoch;   /* Provisioning timestamp */
};

/**
 * Packed binary payload returned when Reading or Notifying the Diagnostics Characteristic
 */
struct __packed fbn_ble_diag_read_resp {
  uint16_t battery_mv;          /* Battery ADC voltage (mV) */
  uint32_t rtc_epoch;           /* PCF8523 current epoch seconds */
  uint32_t uptime_seconds;      /* Kernel uptime in seconds */
  uint8_t  lora_joined;         /* 1 if joined, 0 otherwise */
  int16_t  last_demod_margin;   /* LoRa LinkCheck demod margin (dB), INT16_MIN if none */
  uint8_t  last_nb_gateways;    /* Gateways received in LinkCheckAns */
  uint16_t link_check_samples;  /* Total LinkCheck responses received */
  uint32_t button_counts[6];    /* Press counts for buttons 1, 2, 3, 4, 5, 6 */
  uint32_t last_cleaned_epoch;  /* Timestamp from last cleaning / NFC check-out */
  uint8_t  boot_cause;          /* Boot reset cause (boot_info_get_cause()) */
  uint8_t  hw_variant;          /* 0=FLEXBOX, 1=FLEXBOX_PLUS, 2=FLEXBOX_PLUS_MED */
  uint8_t  fw_version_major;    /* e.g. 1 */
  uint8_t  fw_version_minor;    /* e.g. 5 */
  uint8_t  fw_version_patch;    /* e.g. 3 */
};

/**
 * @brief Initialize the FeedbackNow Custom GATT Service.
 * @return 0 on success, negative errno on failure.
 */
int ble_fbn_service_init(void);

/**
 * @brief Send a notification of current live diagnostics to connected peer.
 * @return 0 on success, negative errno if not connected or notifications disabled.
 */
int ble_fbn_service_notify_diag(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_FBN_SERVICE_H */
