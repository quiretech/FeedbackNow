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
#define NFC_THREAD_STACK_SIZE 4096
#define NFC_THREAD_PRIORITY 4

// ============================================================================
// LORA CONFIGURATION
// ============================================================================

// LoRa communication
#define LORA_MAX_RETRIES 3
#define LORA_RETRY_DELAY_MS 1000
#define LORA_MESSAGE_QUEUE_SIZE 20
#define LORA_SEND_BUSY_RETRY_MS 100
#define LORA_BUTTON_PORT 1
#define LORA_NFC_PORT 100
#define LORA_MAX_PAYLOAD_SIZE 11
#define LORA_MSG_DATA_MAX 1
#define LORA_MSGQ_SIZE 10

#define LORA_JOIN_RETRY_DELAY_SECONDS 5

// LoRaWAN credentials (default values)
#define LORAWAN_DEV_EUI                                                        \
  { 0x3a, 0x2b, 0x35, 0xf2, 0x09, 0x78, 0x6d, 0x1e }
#define LORAWAN_JOIN_EUI                                                       \
  { 0x15, 0x4e, 0x09, 0x81, 0x5d, 0xf3, 0x08, 0x2c }
#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0x89, 0x77, 0xe5, 0x9d, 0x13, 0x46, 0x35, 0x7d, 0x00, 0x8c, 0x32, 0x66,    \
        0xd5, 0xee, 0xa6, 0x1b                                                 \
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
#define METRICS_UPDATE_INTERVAL_MS 100000
#define HEALTH_CHECK_INTERVAL_MS 500000

// ============================================================================
// NVS CONFIGURATION - REMOVED
// ============================================================================
// NVS functionality has been removed from the system

// ============================================================================
// HEARTBEAT CONFIGURATION
// ============================================================================

// Heartbeat timing
#define HEARTBEAT_INTERVAL_MS 6000 // 60 seconds
#define HEARTBEAT_PORT 100
#define HEARTBEAT_CONFIRM_BOOL true

// ============================================================================
// THREAD CONFIGURATION
// ============================================================================

// Thread stack sizes (increased for stability)
#define LED_THREAD_STACK_SIZE 4096
#define BUTTON_THREAD_STACK_SIZE 4096
#define LORA_THREAD_STACK_SIZE 4096
#define STATE_MANAGER_THREAD_STACK_SIZE 4096
#define SYSTEM_MONITOR_THREAD_STACK_SIZE 4096

// Thread priorities (lower number = higher priority)
#define STATE_MANAGER_THREAD_PRIORITY 1
#define LORA_THREAD_PRIORITY 2
#define LED_THREAD_PRIORITY 3
#define BUTTON_THREAD_PRIORITY 4
#define SYSTEM_MONITOR_THREAD_PRIORITY 5

// ============================================================================
// MESSAGE QUEUE ALIGNMENTS
// ============================================================================

// Queue alignment values
#define LED_CMD_ALIGNMENT 4
#define BUTTON_EVENT_ALIGNMENT 1
#define LORA_MESSAGE_ALIGNMENT 4
#define STATE_EVENT_ALIGNMENT 4

// ============================================================================
// ZEPHYR CONFIGURATION OVERRIDES
// ============================================================================

// Main stack size (increased for stability)
#define CONFIG_MAIN_STACK_SIZE 32768
#define CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE 32768

#endif // SYS_CONFIG_H
