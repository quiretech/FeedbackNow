/**
 * @file ble_manager.h
 * @brief Bluetooth Low Energy lifecycle and advertising manager for FeedbackNow.
 *
 * Responsibilities:
 *  - Initialize the Zephyr Bluetooth stack.
 *  - Control connectable advertising with power-saving automatic timeout.
 *  - Manage peer connection and disconnection events.
 *  - Provide on-demand advertising activation (boot, button combo, NFC swipe).
 */

#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default duration (in seconds) that BLE advertises before automatically sleeping */
#ifndef BLE_ADV_TIMEOUT_SECONDS
#define BLE_ADV_TIMEOUT_SECONDS 300U
#endif

/**
 * @brief Initialize BLE stack, register callbacks, and start initial boot advertising.
 * @return 0 on success, negative errno on failure.
 */
int ble_manager_init(void);

/**
 * @brief Start connectable BLE advertising with an automatic timeout.
 *
 * Can be called at boot, on button combo, or on staff NFC swipe.
 * If already advertising, restarts the timeout window.
 *
 * @return 0 on success, negative errno on failure.
 */
int ble_manager_start_advertising(void);

/**
 * @brief Stop BLE advertising immediately and return radio to lowest power state.
 * @return 0 on success, negative errno on failure.
 */
int ble_manager_stop_advertising(void);

/**
 * @brief Check if the device is currently advertising over BLE.
 */
bool ble_manager_is_advertising(void);

/**
 * @brief Check if an active BLE connection is established.
 */
bool ble_manager_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_MANAGER_H */
