#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/* Button subsystem */
#define NUM_BUTTONS           6
#define BUTTON_QUEUE_SIZE     16
#define BUTTON_QUEUE_ALIGNMENT 4
#define BUTTON_DEBOUNCE_MS    25
#define BUTTON_THREAD_STACK_SIZE 1536
#define BUTTON_THREAD_PRIORITY  5

/* Combo hold durations (ms) - configurable per FRD */
#define COMBO_STAFF_HOLD_MS       2000
#define COMBO_DEVICE_INFO_HOLD_MS 3000
#define COMBO_JOIN_HOLD_MS        3000
#define COMBO_REBOOT_HOLD_MS      10000

#endif /* SYS_CONFIG_H */
