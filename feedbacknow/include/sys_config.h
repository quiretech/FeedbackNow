#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

// ============================================================================
// SYSTEM TIMING CONFIGURATION
// ============================================================================

// Main system timing
#define MAIN_LOOP_SLEEP_SECONDS 10
#define SYSTEM_READY_TIMEOUT_MS 5000
#define NVS_INIT_DELAY_MS 500

// ============================================================================
// LED CONFIGURATION
// ============================================================================

// LED behavior
#define LED_BLINK_DURATION_MS 1000
#define LED_QUEUE_SIZE 10
#define LED_QUEUE_ALIGNMENT 4
#define NUM_LEDS 7

// ============================================================================
// BUTTON CONFIGURATION
// ============================================================================

// Button processing
#define BUTTON_QUEUE_SIZE 10
#define BUTTON_QUEUE_ALIGNMENT 1
#define NUM_BUTTONS 7
#define MAX_BUTTON_ID 6

// ============================================================================
// NFC CONFIGURATION
// ============================================================================

// NFC processing
#define NFC_SCAN_DURATION_MS 5000
#define NFC_UID_LENGTH 8
#define NFC_QUEUE_SIZE 5
#define NFC_QUEUE_ALIGNMENT 4
#define NFC_THREAD_STACK_SIZE 2048
#define NFC_THREAD_PRIORITY 4

// ============================================================================
// LORA CONFIGURATION
// ============================================================================

// LoRa communication
#define LORA_MAX_RETRIES 3
#define LORA_RETRY_DELAY_MS 1000
#define LORA_MESSAGE_QUEUE_SIZE 10
#define LORA_SEND_BUSY_RETRY_MS 100
#define LORA_BUTTON_PORT 1
#define LORA_NFC_PORT 100
#define LORA_MAX_PAYLOAD_SIZE 11
#define LORA_MSG_DATA_MAX 1
#define LORA_MSGQ_SIZE 10

#define LORA_JOIN_RETRY_DELAY_SECONDS 5

// LoRaWAN credentials (default values)
#define LORAWAN_DEV_EUI                                                        \
  { 0x93, 0x51, 0x10, 0xc4, 0xfc, 0x44, 0x2f, 0xb7 }
#define LORAWAN_JOIN_EUI                                                       \
  { 0xc0, 0x70, 0xd7, 0x4c, 0xa6, 0x92, 0x93, 0xa3 }
#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0xb5, 0x91, 0x17, 0xb5, 0x79, 0x0b, 0x15, 0x0d, 0x30, 0xeb, 0xa1, 0x69,    \
        0x0b, 0x10, 0xc9, 0x34                                                 \
  }

// ============================================================================
// STATE MANAGER CONFIGURATION
// ============================================================================

// State management
#define STATE_EVENT_QUEUE_SIZE 20
#define STATE_EVENT_QUEUE_ALIGNMENT 4

// ============================================================================
// SYSTEM MONITOR CONFIGURATION
// ============================================================================

// System monitoring
#define METRICS_UPDATE_INTERVAL_MS 1000
#define HEALTH_CHECK_INTERVAL_MS 5000

// ============================================================================
// NVS CONFIGURATION
// ============================================================================

// NVS storage
#define NVS_SECTOR_COUNT 3
#define NVS_MSGQ_MAX_MSGS 10
#define NVS_DEVNONCE_ID 0
#define NVS_LORAWAN_DEV_EUI_ID 1
#define NVS_LORAWAN_JOIN_EUI_ID 2
#define NVS_LORAWAN_APP_KEY_ID 3
#define NVS_MAX_KEY_SIZE 16
#define NVS_DEVNONCE_SIZE 2
#define NVS_DEVEUI_SIZE 8
#define NVS_JOINEUI_SIZE 8
#define NVS_APPKEY_SIZE 16

// ============================================================================
// HEARTBEAT CONFIGURATION
// ============================================================================

// Heartbeat timing
#define HEARTBEAT_INTERVAL_MS 60000 // 60 seconds

// ============================================================================
// THREAD CONFIGURATION
// ============================================================================

// Thread stack sizes
#define LED_THREAD_STACK_SIZE 1024
#define BUTTON_THREAD_STACK_SIZE 2048
#define NVS_THREAD_STACK_SIZE 1024
#define LORA_THREAD_STACK_SIZE 16384
#define STATE_MANAGER_THREAD_STACK_SIZE 2048
#define SYSTEM_MONITOR_THREAD_STACK_SIZE 1024

// Thread priorities (lower number = higher priority)
#define STATE_MANAGER_THREAD_PRIORITY 1
#define NVS_THREAD_PRIORITY 2
#define LORA_THREAD_PRIORITY 3
#define LED_THREAD_PRIORITY 4
#define BUTTON_THREAD_PRIORITY 5
#define SYSTEM_MONITOR_THREAD_PRIORITY 6

// ============================================================================
// MESSAGE QUEUE ALIGNMENTS
// ============================================================================

// Queue alignment values
#define LED_CMD_ALIGNMENT 4
#define BUTTON_EVENT_ALIGNMENT 1
#define LORA_MESSAGE_ALIGNMENT 4
#define STATE_EVENT_ALIGNMENT 4
#define NVS_MSG_ALIGNMENT 4

// ============================================================================
// ZEPHYR CONFIGURATION OVERRIDES
// ============================================================================

// Main stack size
#define CONFIG_MAIN_STACK_SIZE 16384
#define CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE 16384

#endif // SYS_CONFIG_H
