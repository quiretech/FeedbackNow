#ifndef K_CONFIG_H
#define K_CONFIG_H

/* Stack sizes */
#define LED_THREAD_STACK_SIZE 1024
#define BUTTON_THREAD_STACK_SIZE 2048
#define NVS_THREAD_STACK_SIZE 1024
#define LORA_THREAD_STACK_SIZE 16384

/* Thread priorities (lower number = higher priority) */
#define LED_THREAD_PRIORITY 4
#define BUTTON_THREAD_PRIORITY 5 // Lower priority than LoRa
#define NVS_THREAD_PRIORITY 1
#define LORA_THREAD_PRIORITY 3 // Higher priority than button

#endif // K_CONFIG_H
