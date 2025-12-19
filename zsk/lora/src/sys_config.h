#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/*
 * Demo mode switch
 * - 0: current pseudo simulation (periodic rotating payloads)
 * - 1: real button presses generate button uplinks
 */
#define DEMO_USE_REAL_BUTTON_UPLINK 0

/* LoRa Configuration */
#define LORA_MAX_PAYLOAD_SIZE 11
#define LORA_MSGQ_SIZE 10
#define LORA_MESSAGE_QUEUE_SIZE 10
#define LORA_MESSAGE_ALIGNMENT 4
#define LORA_THREAD_STACK_SIZE 2048
#define LORA_THREAD_PRIORITY 5
#define LORA_JOIN_RETRY_DELAY_SECONDS 10
#define LORA_MAX_RETRIES 3
#define LORA_SEND_BUSY_RETRY_MS 1000
#define LORA_BUTTON_PORT 2

/* Send interval in seconds */
#define LORA_SEND_INTERVAL_SECONDS 900

/* Button configuration (gpio-keys aliases in DT overlay) */
#define NUM_BUTTONS 7
#define BUTTON_QUEUE_SIZE 16
#define BUTTON_QUEUE_ALIGNMENT 4
#define BUTTON_THREAD_STACK_SIZE 1536
#define BUTTON_THREAD_PRIORITY 6
#define BUTTON_DEBOUNCE_MS 50

/* RTC demo configuration (Option B: hardcode time at boot) */
#define RTC_SET_TIME_ON_BOOT 1
/* If 1, always overwrite RTC time at boot (use once, then set back to 0) */
#define RTC_FORCE_SET_TIME_ON_BOOT 0

/* Set this to the current UTC time you want programmed into the RTC.
 * Notes:
 * - Month is 1-12
 * - Day is 1-31
 * - Hour is 0-23 (24h)
 * - Minute/second are 0-59
 */
#define RTC_SET_YEAR 2025
#define RTC_SET_MONTH 12
#define RTC_SET_DAY 19
#define RTC_SET_HOUR 21
#define RTC_SET_MINUTE 10
#define RTC_SET_SECOND 00

/* Treat RTC as "already initialized" if year >= this value */
#define RTC_VALID_YEAR_MIN 2024

#endif /* SYS_CONFIG_H */
