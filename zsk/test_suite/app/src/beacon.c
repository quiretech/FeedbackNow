
// /*
//  * LoRa QA Beacon Node
//  *
//  * Listens for a ping packet from a DUT, then replies with an incrementing
//  * counter. Sits on the bench powered on during QA. Flash once, leave
//  running.
//  *
//  * Packet format (4 bytes):
//  *   [0]      : type  0xAA = ping (from DUT), 0xBB = pong (from beacon)
//  *   [1]      : reserved / 0x00
//  *   [2..3]   : uint16_t counter, big-endian (beacon increments on each
//  reply)
//  *
//  * Config: 915.0 MHz, BW 125kHz, SF7, CR 4/5, private sync word
//  *
//  * SPDX-License-Identifier: Apache-2.0
//  */

// #include <zephyr/device.h>
// #include <zephyr/devicetree.h>
// #include <zephyr/drivers/lora.h>
// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(beacon, CONFIG_LOG_DEFAULT_LEVEL);

// /* ------------------------------------------------------------------ */
// /*  RF config                                                           */
// /* ------------------------------------------------------------------ */
// #define LORA_FREQ 915000000U /* 915.0 MHz */
// #define LORA_TX_POWER 14     /* dBm */
// #define LORA_BW BW_125_KHZ
// #define LORA_SF SF_7
// #define LORA_CR CR_4_5
// #define LORA_PREAMBLE 8

// /* ------------------------------------------------------------------ */
// /*  Packet definition                                                   */
// /* ------------------------------------------------------------------ */
// #define PKT_LEN 4
// #define PKT_TYPE_PING 0xAA /* DUT  → beacon */
// #define PKT_TYPE_PONG 0xBB /* beacon → DUT  */

// static uint16_t pong_counter = 0;

// /* Build a pong packet into buf[4] */
// static void build_pong(uint8_t *buf) {
//   buf[0] = PKT_TYPE_PONG;
//   buf[1] = 0x00;
//   buf[2] = (uint8_t)(pong_counter >> 8);
//   buf[3] = (uint8_t)(pong_counter & 0xFF);
//   pong_counter++;
// }

// int main(void) {
//   LOG_INF("=== LoRa QA Beacon starting ===");

//   const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
//   if (!device_is_ready(lora_dev)) {
//     LOG_ERR("LoRa device not ready — check overlay");
//     return -ENODEV;
//   }

//   /* Configure radio */
//   struct lora_modem_config cfg = {
//       .frequency = LORA_FREQ,
//       .bandwidth = LORA_BW,
//       .datarate = LORA_SF,
//       .coding_rate = LORA_CR,
//       .preamble_len = LORA_PREAMBLE,
//       .tx_power = LORA_TX_POWER,
//       .tx = false, /* start in RX */
//       .iq_inverted = false,
//       .public_network = false, /* private sync word for bench use */
//   };

//   int ret = lora_config(lora_dev, &cfg);
//   if (ret != 0) {
//     LOG_ERR("lora_config failed: %d", ret);
//     return ret;
//   }

//   LOG_INF("Beacon listening on %.3f MHz SF%d BW125 — waiting for DUT
//   pings...",
//           (double)LORA_FREQ / 1e6, 7);

//   while (1) {
//     uint8_t rx_buf[PKT_LEN];
//     int16_t rssi;
//     int8_t snr;

//     /* Block until we receive something (no timeout — beacon waits forever)
//     */ cfg.tx = false; lora_config(lora_dev, &cfg);

//     int len =
//         lora_recv(lora_dev, rx_buf, sizeof(rx_buf), K_FOREVER, &rssi, &snr);

//     if (len < 0) {
//       LOG_WRN("lora_recv error: %d — retrying", len);
//       k_msleep(100);
//       continue;
//     }

//     /* Log what we heard */
//     LOG_INF("RX [%d bytes] type=0x%02X rssi=%d snr=%d", len,
//             len >= 1 ? rx_buf[0] : 0xFF, rssi, snr);

//     /* Only reply to valid ping packets */
//     if (len != PKT_LEN || rx_buf[0] != PKT_TYPE_PING) {
//       LOG_WRN("Ignoring non-ping packet (type=0x%02X len=%d)",
//               len >= 1 ? rx_buf[0] : 0, len);
//       continue;
//     }

//     /* Small guard delay — let DUT flip to RX before we transmit */
//     k_msleep(10);

//     /* Build and send pong */
//     uint8_t tx_buf[PKT_LEN];
//     build_pong(tx_buf);

//     cfg.tx = true;
//     lora_config(lora_dev, &cfg);

//     ret = lora_send(lora_dev, tx_buf, PKT_LEN);
//     if (ret != 0) {
//       LOG_ERR("lora_send failed: %d", ret);
//     } else {
//       LOG_INF("TX pong #%u [0x%02X 0x%02X 0x%02X 0x%02X]", pong_counter - 1,
//               tx_buf[0], tx_buf[1], tx_buf[2], tx_buf[3]);
//     }

//     /* Flip back to RX for next DUT */
//     cfg.tx = false;
//     lora_config(lora_dev, &cfg);
//   }

//   return 0;
// }