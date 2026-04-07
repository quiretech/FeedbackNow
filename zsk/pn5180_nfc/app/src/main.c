#include "pn5180.h"
#include "power_ctrl.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Thread stack sizes */
#define NFC_THREAD_STACK_SIZE 2048
#define PROCESSING_THREAD_STACK_SIZE 2048

/* Message queue for NFC events */
#define NFC_QUEUE_SIZE 10

/* Power management and wake-up configuration */
#define WAKE_UP_GPIO_NODE DT_ALIAS(wakeup)
#define WAKE_UP_GPIO_DEV DT_GPIO_CTLR(WAKE_UP_GPIO_NODE, gpios)
#define WAKE_UP_GPIO_PIN DT_GPIO_PIN(WAKE_UP_GPIO_NODE, gpios)
#define WAKE_UP_GPIO_FLAGS DT_GPIO_FLAGS(WAKE_UP_GPIO_NODE, gpios)

/* System states */
enum system_state {
  SYSTEM_SLEEP,
  SYSTEM_NFC_ACTIVE,
  SYSTEM_NFC_SCANNING,
  SYSTEM_NFC_PROCESSING
};

/* Message structure for NFC events */
struct nfc_event {
  enum { NFC_EVENT_TAG_DETECTED, NFC_EVENT_TAG_LOST, NFC_EVENT_ERROR } type;
  uint8_t uid[8];
  uint32_t timestamp;
};

/* Global variables */
static const struct device *pn5180_dev = DEVICE_DT_GET(DT_NODELABEL(pn5180));
static const struct device *wake_up_gpio_dev =
    DEVICE_DT_GET(DT_GPIO_CTLR(WAKE_UP_GPIO_NODE, gpios));
static const gpio_pin_t wake_up_pin = WAKE_UP_GPIO_PIN;
static const gpio_flags_t wake_up_flags = WAKE_UP_GPIO_FLAGS;

K_MSGQ_DEFINE(nfc_queue, sizeof(struct nfc_event), NFC_QUEUE_SIZE, 4);
K_MUTEX_DEFINE(nfc_mutex);
K_SEM_DEFINE(nfc_ready_sem, 0, 1);
K_SEM_DEFINE(wake_up_sem, 0, 1);
K_SEM_DEFINE(nfc_scan_complete_sem, 0, 1);

/* System state management */
static enum system_state current_state = SYSTEM_SLEEP;
static K_MUTEX_DEFINE(state_mutex);

/* GPIO callback structure */
static struct gpio_callback wake_up_cb;

/* Thread data structures */
static struct k_thread nfc_thread;
static struct k_thread processing_thread;
K_THREAD_STACK_DEFINE(nfc_thread_stack, NFC_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(processing_thread_stack, PROCESSING_THREAD_STACK_SIZE);

/* Custom tag processing functions */
static void process_tag_detected(uint8_t *uid, uint32_t timestamp);
static void process_tag_lost(uint32_t timestamp);
static bool is_authorized_tag(uint8_t *uid);
static void trigger_access_granted(void);
static void trigger_access_denied(void);
static void reset_access_state(void);
static void handle_application_logic(void);

/* Power management functions */
static void gpio_wake_up_callback(const struct device *dev,
                                  struct gpio_callback *cb, uint32_t pins);
static void set_system_state(enum system_state new_state);
static void enter_sleep_mode(void);
static void wake_up_nfc_system(void);
static void power_down_nfc_system(void);

/* NFC scanning thread - event-driven, wakes up on demand */
static void nfc_thread_entry(void *arg1, void *arg2, void *arg3) {
  uint8_t uid[8] = {0};
  struct nfc_event event;
  int ret;

  LOG_INF("NFC thread started");

  /* Wait for initialization to complete */
  k_sem_take(&nfc_ready_sem, K_FOREVER);

  while (1) {
    /* Wait for wake-up signal */
    k_sem_take(&wake_up_sem, K_FOREVER);

    LOG_INF("NFC system awakened - starting scan");
    set_system_state(SYSTEM_NFC_SCANNING);

    /* Perform NFC scan */
    k_mutex_lock(&nfc_mutex, K_FOREVER);
    ret = pn5180_get_inventory(pn5180_dev, uid, sizeof(uid));
    k_mutex_unlock(&nfc_mutex);

    if (ret == 0) {
      /* Tag detected */
      event.type = NFC_EVENT_TAG_DETECTED;
      memcpy(event.uid, uid, sizeof(uid));
      event.timestamp = k_uptime_get();

      if (k_msgq_put(&nfc_queue, &event, K_NO_WAIT) != 0) {
        LOG_WRN("NFC queue full, dropping tag detected event");
      }

      LOG_INF("Tag detected: %02X %02X %02X %02X %02X %02X %02X %02X", uid[0],
              uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);

      set_system_state(SYSTEM_NFC_PROCESSING);

      /* Wait for processing to complete */
      k_sem_take(&nfc_scan_complete_sem, K_FOREVER);

    } else {
      LOG_INF("No tag detected");
    }

    /* Power down NFC system and return to sleep */
    power_down_nfc_system();
    set_system_state(SYSTEM_SLEEP);

    LOG_INF("NFC system returning to sleep");
  }
}

/* Tag processing thread - handles detected tags */
static void processing_thread_entry(void *arg1, void *arg2, void *arg3) {
  struct nfc_event event;

  LOG_INF("Processing thread started");

  while (1) {
    /* Wait for NFC events */
    if (k_msgq_get(&nfc_queue, &event, K_FOREVER) == 0) {
      switch (event.type) {
      case NFC_EVENT_TAG_DETECTED:
        LOG_INF("Processing tag: %02X %02X %02X %02X %02X %02X %02X %02X",
                event.uid[0], event.uid[1], event.uid[2], event.uid[3],
                event.uid[4], event.uid[5], event.uid[6], event.uid[7]);

        /* Your custom tag processing logic here */
        process_tag_detected(event.uid, event.timestamp);

        /* Signal that processing is complete */
        k_sem_give(&nfc_scan_complete_sem);
        break;

      case NFC_EVENT_TAG_LOST:
        LOG_INF("Tag lost at %u ms", event.timestamp);
        process_tag_lost(event.timestamp);
        break;

      case NFC_EVENT_ERROR:
        LOG_ERR("NFC error occurred");
        k_sem_give(&nfc_scan_complete_sem);
        break;
      }
    }
  }
}

/* Custom tag processing functions */
static void process_tag_detected(uint8_t *uid, uint32_t timestamp) {
  /* Example: Check if tag is authorized */
  if (is_authorized_tag(uid)) {
    LOG_INF("Authorized tag detected");
    /* Trigger access granted actions */
    trigger_access_granted();
  } else {
    LOG_WRN("Unauthorized tag detected");
    /* Trigger access denied actions */
    trigger_access_denied();
  }
}

static void process_tag_lost(uint32_t timestamp) {
  LOG_INF("Tag removed, resetting access state");
  /* Reset access control state */
  reset_access_state();
}

static bool is_authorized_tag(uint8_t *uid) {
  /* Example authorized UIDs - add your own authorized tags here */
  uint8_t authorized_uids[][8] = {
      {0xE0, 0x04, 0x01, 0x00, 0x88, 0x8C, 0x9C, 0xA5},
      {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77}};

  for (int i = 0; i < ARRAY_SIZE(authorized_uids); i++) {
    if (memcmp(uid, authorized_uids[i], 8) == 0) {
      return true;
    }
  }
  return false;
}

static void trigger_access_granted(void) {
  LOG_INF("*** ACCESS GRANTED ***");
  /* Add your access granted logic here */
  /* Examples: Turn on LED, unlock door, send notification, etc. */
}

static void trigger_access_denied(void) {
  LOG_INF("*** ACCESS DENIED ***");
  /* Add your access denied logic here */
  /* Examples: Turn on red LED, sound alarm, log attempt, etc. */
}

static void reset_access_state(void) {
  LOG_INF("Resetting access control state");
  /* Add your state reset logic here */
  /* Examples: Turn off LEDs, reset indicators, etc. */
}

static void handle_application_logic(void) {
  /* Main application logic that runs independently of NFC */
  /* Examples: Handle other sensors, network communication, etc. */
  static uint32_t counter = 0;
  counter++;

  if (counter % 10 == 0) {
    LOG_INF("Application running... (counter: %u)", counter);
  }
}

/* GPIO interrupt callback - wakes up the system */
static void gpio_wake_up_callback(const struct device *dev,
                                  struct gpio_callback *cb, uint32_t pins) {
  LOG_INF("Wake-up GPIO triggered!");

  /* Wake up the NFC system */
  k_sem_give(&wake_up_sem);
}

/* Set system state with logging */
static void set_system_state(enum system_state new_state) {
  k_mutex_lock(&state_mutex, K_FOREVER);

  const char *state_names[] = {"SLEEP", "NFC_ACTIVE", "NFC_SCANNING",
                               "NFC_PROCESSING"};

  LOG_INF("System state: %s -> %s", state_names[current_state],
          state_names[new_state]);

  current_state = new_state;
  k_mutex_unlock(&state_mutex);
}

/* Enter sleep mode */
static void enter_sleep_mode(void) {
  LOG_INF("Entering sleep mode");

  /* Configure wake-up GPIO interrupt */
  gpio_pin_interrupt_configure(wake_up_gpio_dev, wake_up_pin,
                               GPIO_INT_EDGE_TO_ACTIVE);

  set_system_state(SYSTEM_SLEEP);
}

/* Wake up NFC system */
static void wake_up_nfc_system(void) {
  LOG_INF("Waking up NFC system");

  /* Disable wake-up GPIO interrupt temporarily */
  gpio_pin_interrupt_configure(wake_up_gpio_dev, wake_up_pin, GPIO_INT_DISABLE);

  set_system_state(SYSTEM_NFC_ACTIVE);
}

/* Power down NFC system */
static void power_down_nfc_system(void) {
  LOG_INF("Powering down NFC system");

  /* Disable RF field */
  k_mutex_lock(&nfc_mutex, K_FOREVER);
  pn5180_configure(pn5180_dev, PN5180_PROTOCOL_ISO15693); // Reset to idle
  k_mutex_unlock(&nfc_mutex);
}

int main(void) {
  int ret;

  LOG_INF("PN5180 Power-Efficient RTOS Application Starting...");

  /* Initialize power control and enable all rails */
  ret = power_ctrl_init();
  if (ret != 0) {
    LOG_ERR("Failed to initialize power control: %d", ret);
    return ret;
  }

  /* Enable all power rails with sequencing delays */
  LOG_INF("Enabling power rails...");

  ret = power_ctrl_set(POWER_EN_3V3, true);
  if (ret != 0) {
    LOG_ERR("Failed to enable 3V3 rail: %d", ret);
    return ret;
  }
  k_msleep(10);

  ret = power_ctrl_set(POWER_EN_1V8, true);
  if (ret != 0) {
    LOG_ERR("Failed to enable 1V8 rail: %d", ret);
    return ret;
  }
  k_msleep(10);

  ret = power_ctrl_set(POWER_EN_3V3A, true);
  if (ret != 0) {
    LOG_ERR("Failed to enable 3V3A rail: %d", ret);
    return ret;
  }
  k_msleep(10);

  ret = power_ctrl_set(POWER_EN_3V6, true);
  if (ret != 0) {
    LOG_ERR("Failed to enable 3V6 rail: %d", ret);
    return ret;
  }
  k_msleep(50); /* Allow rails to stabilize */

  LOG_INF("All power rails enabled");

  /* Configure LR Reset and CS pins */
  // ret = power_ctrl_set(POWER_LR_RESET, true); /* Hold in reset initially */
  // if (ret != 0) {
  //   LOG_ERR("Failed to set LR Reset: %d", ret);
  //   return ret;
  // }

  // ret = power_ctrl_set(POWER_LR_CS,
  //                      false); /* Deselect CS (active low typically) */

  // if (ret != 0) {
  //   LOG_ERR("Failed to set LR CS: %d", ret);
  //   return ret;
  // }

  LOG_INF("LR Reset and CS pins configured");

  /* Check if devices are ready */
  if (!device_is_ready(pn5180_dev)) {
    LOG_ERR("PN5180 device not ready");
    return -ENODEV;
  }

  if (!device_is_ready(wake_up_gpio_dev)) {
    LOG_ERR("Wake-up GPIO device not ready");
    return -ENODEV;
  }

  /* Initialize the NFC driver */
  if (pn5180_init(pn5180_dev) != 0) {
    LOG_ERR("Failed to initialize PN5180");
    return -EIO;
  }

  /* Configure for ISO15693 protocol */
  if (pn5180_configure(pn5180_dev, PN5180_PROTOCOL_ISO15693) != 0) {
    LOG_ERR("Failed to configure PN5180");
    return -EIO;
  }

  /* Configure wake-up GPIO */
  ret = gpio_pin_configure(wake_up_gpio_dev, wake_up_pin,
                           GPIO_INPUT | wake_up_flags);
  if (ret) {
    LOG_ERR("Failed to configure wake-up GPIO: %d", ret);
    return ret;
  }

  /* Set up GPIO interrupt callback */
  gpio_init_callback(&wake_up_cb, gpio_wake_up_callback, BIT(wake_up_pin));
  gpio_add_callback(wake_up_gpio_dev, &wake_up_cb);

  LOG_INF("PN5180 and wake-up GPIO initialized");

  /* Create NFC scanning thread */
  k_thread_create(&nfc_thread, nfc_thread_stack,
                  K_THREAD_STACK_SIZEOF(nfc_thread_stack), nfc_thread_entry,
                  NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
  k_thread_name_set(&nfc_thread, "nfc_scanner");

  /* Create tag processing thread */
  k_thread_create(&processing_thread, processing_thread_stack,
                  K_THREAD_STACK_SIZEOF(processing_thread_stack),
                  processing_thread_entry, NULL, NULL, NULL, K_PRIO_COOP(6), 0,
                  K_NO_WAIT);
  k_thread_name_set(&processing_thread, "tag_processor");

  /* Signal that NFC is ready */
  k_sem_give(&nfc_ready_sem);

  LOG_INF("All threads started, entering sleep mode");

  /* Enter sleep mode initially */
  enter_sleep_mode();

  /* Main thread handles power management */
  while (1) {
    /* Handle other application logic when not sleeping */
    if (current_state != SYSTEM_SLEEP) {
      handle_application_logic();
    }

    /* Sleep for a short time to allow other threads to run */
    k_msleep(100);
  }

  return 0;
}