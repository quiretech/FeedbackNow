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

/*============================================================================
 * NFC READ MODE CONFIGURATION
 *
 * Set to 1 for SINGLE BLOCK mode (production) - reads only the custom data
 * block. Set to 0 for FULL DUMP mode (debugging) - reads all blocks and hex
 * dumps.
 *============================================================================*/
#define NFC_READ_SINGLE_BLOCK_MODE 0

/* Set to 1 to enable write demo (writes test data to a block) */
#define NFC_WRITE_DEMO_ENABLED 1
#define NFC_WRITE_DEMO_BLOCK 27 /* Block to write test data to */

/* Block number where your custom 4-byte data is stored (used in single block
 * mode) */
#define NFC_CUSTOM_DATA_BLOCK 27

/* Maximum tag parameters (used for buffer allocation)
 * Note: Keep MAX_TAG_SIZE reasonable to avoid stack overflow!
 * - SLIX: 28 blocks × 4 bytes = 112 bytes
 * - SLIX2: 80 blocks × 4 bytes = 320 bytes
 * We use 512 bytes as a safe maximum for most ISO15693 tags.
 */
#define MAX_BLOCK_SIZE 32
#define MAX_NUM_BLOCKS 128
#define MAX_TAG_SIZE 512 /* Maximum tag data we'll read */

/* Default SLIX tag parameters (fallback if get_system_info fails) */
#define DEFAULT_BLOCK_SIZE 4
#define DEFAULT_NUM_BLOCKS 28

/* Thread stack sizes - must be large enough for local buffers */
#define NFC_THREAD_STACK_SIZE 4096
#define PROCESSING_THREAD_STACK_SIZE 4096

/* Message queue for NFC events */
#define NFC_QUEUE_SIZE 4 /* Reduced to save memory */

/* Power management and wake-up clear
configuration */
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
  /* Tag memory parameters (discovered via get_system_info) */
  uint8_t block_size;
  uint8_t num_blocks;
#if NFC_READ_SINGLE_BLOCK_MODE
  uint8_t custom_data[MAX_BLOCK_SIZE]; /* Custom block data */
#else
  uint8_t tag_data[MAX_TAG_SIZE]; /* Full tag data for dump mode */
  size_t tag_data_len;
#endif
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

/* PN5180 verification and info functions */
static int verify_pn5180_communication(void);
static void print_pn5180_version(const struct pn5180_version_info *info);

/* Tag processing functions */
#if NFC_READ_SINGLE_BLOCK_MODE
static void process_tag_detected(uint8_t *uid, uint8_t *custom_data,
                                 uint8_t block_size, uint32_t timestamp);
#else
static void process_tag_detected(uint8_t *uid, uint8_t *tag_data,
                                 size_t tag_data_len, uint8_t block_size,
                                 uint8_t num_blocks, uint32_t timestamp);
static void print_tag_data(uint8_t *data, size_t len, uint8_t block_size);
#endif
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
  struct iso15693_system_info tag_info;
  int ret;

  LOG_INF("NFC thread started");

  /* Wait for initialization to complete */
  k_sem_take(&nfc_ready_sem, K_FOREVER);

  while (1) {
    /* Wait for wake-up signal */
    k_sem_take(&wake_up_sem, K_FOREVER);

    LOG_INF("NFC system awakened - starting scan");
    set_system_state(SYSTEM_NFC_SCANNING);

    /* Perform NFC scan (inventory) */
    k_mutex_lock(&nfc_mutex, K_FOREVER);
    ret = pn5180_get_inventory(pn5180_dev, uid, sizeof(uid));
    k_mutex_unlock(&nfc_mutex);

    if (ret == 0) {
      /* Tag detected */
      event.type = NFC_EVENT_TAG_DETECTED;
      memcpy(event.uid, uid, sizeof(uid));
      event.timestamp = k_uptime_get();

      LOG_INF("Tag UID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", uid[0],
              uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);

      /* Get tag system info to determine memory layout */
      k_mutex_lock(&nfc_mutex, K_FOREVER);
      ret = pn5180_get_system_info(pn5180_dev, uid, &tag_info);
      k_mutex_unlock(&nfc_mutex);

      if (ret == 0) {
        event.block_size = tag_info.block_size;
        event.num_blocks = tag_info.num_blocks;
        LOG_INF("Tag info: %d blocks x %d bytes = %d bytes total",
                tag_info.num_blocks, tag_info.block_size,
                tag_info.num_blocks * tag_info.block_size);
        if (tag_info.info_flags & ISO15693_INFO_FLAG_AFI) {
          LOG_INF("  AFI: 0x%02X", tag_info.afi);
        }
        if (tag_info.info_flags & ISO15693_INFO_FLAG_DSFID) {
          LOG_INF("  DSFID: 0x%02X", tag_info.dsfid);
        }
        if (tag_info.info_flags & ISO15693_INFO_FLAG_IC_REF) {
          LOG_INF("  IC Reference: 0x%02X", tag_info.ic_reference);
        }
      } else {
        LOG_WRN("Failed to get system info: %s, using defaults",
                pn5180_strerror(ret));
        event.block_size = DEFAULT_BLOCK_SIZE;
        event.num_blocks = DEFAULT_NUM_BLOCKS;
      }

#if NFC_WRITE_DEMO_ENABLED
      /* Write demo: Write test data to a block */
      {
        uint8_t test_data[MAX_BLOCK_SIZE] = {0x48, 0x65, 0x79, 0x21}; // "Hey!"
        LOG_INF("Write demo: Writing to block %d...", NFC_WRITE_DEMO_BLOCK);

        k_mutex_lock(&nfc_mutex, K_FOREVER);
        ret = pn5180_write_block(pn5180_dev, uid, NFC_WRITE_DEMO_BLOCK,
                                 test_data, event.block_size);
        k_mutex_unlock(&nfc_mutex);

        if (ret == 0) {
          LOG_INF("Write demo: Block %d written successfully!",
                  NFC_WRITE_DEMO_BLOCK);
        } else {
          LOG_ERR("Write demo: Failed to write block %d: %s",
                  NFC_WRITE_DEMO_BLOCK, pn5180_strerror(ret));
        }
      }
#endif

#if NFC_READ_SINGLE_BLOCK_MODE
      /* Single block mode: Read only the custom data block */
      k_mutex_lock(&nfc_mutex, K_FOREVER);
      ret = pn5180_read_block(pn5180_dev, uid, NFC_CUSTOM_DATA_BLOCK,
                              event.custom_data, event.block_size);
      k_mutex_unlock(&nfc_mutex);

      if (ret == 0) {
        LOG_INF("Block %d: %02X %02X %02X %02X", NFC_CUSTOM_DATA_BLOCK,
                event.custom_data[0], event.custom_data[1],
                event.custom_data[2], event.custom_data[3]);
      } else {
        LOG_WRN("Failed to read block %d: %s", NFC_CUSTOM_DATA_BLOCK,
                pn5180_strerror(ret));
        memset(event.custom_data, 0, sizeof(event.custom_data));
      }
#else
      /* Full dump mode: Read all blocks dynamically */
      event.tag_data_len = 0;
      size_t total_size = (size_t)event.block_size * event.num_blocks;
      uint8_t blocks_to_read = event.num_blocks;

      if (total_size > sizeof(event.tag_data)) {
        LOG_WRN("Tag size (%d) exceeds buffer (%d), truncating", total_size,
                sizeof(event.tag_data));
        blocks_to_read = sizeof(event.tag_data) / event.block_size;
      }

      /* Read blocks one at a time for reliability */
      k_mutex_lock(&nfc_mutex, K_FOREVER);
      for (uint8_t block = 0; block < blocks_to_read; block++) {
        ret = pn5180_read_block(pn5180_dev, uid, block,
                                &event.tag_data[block * event.block_size],
                                event.block_size);
        if (ret != 0) {
          LOG_ERR("Failed to read block %d: %s", block, pn5180_strerror(ret));
          break;
        }
        event.tag_data_len += event.block_size;
      }
      k_mutex_unlock(&nfc_mutex);

      if (event.tag_data_len > 0) {
        LOG_INF("Read %d bytes from tag (%d blocks)", event.tag_data_len,
                event.tag_data_len / event.block_size);
      }
#endif

      if (k_msgq_put(&nfc_queue, &event, K_NO_WAIT) != 0) {
        LOG_WRN("NFC queue full, dropping tag detected event");
      }

      set_system_state(SYSTEM_NFC_PROCESSING);

      /* Wait for processing to complete */
      k_sem_take(&nfc_scan_complete_sem, K_FOREVER);

    } else {
      LOG_INF("No tag detected (%s)", pn5180_strerror(ret));
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
#if NFC_READ_SINGLE_BLOCK_MODE
        /* Single block mode - process custom block data */
        process_tag_detected(event.uid, event.custom_data, event.block_size,
                             event.timestamp);
#else
        /* Full dump mode - process all tag data */
        process_tag_detected(event.uid, event.tag_data, event.tag_data_len,
                             event.block_size, event.num_blocks,
                             event.timestamp);
#endif
        /* Signal that processing is complete */
        k_sem_give(&nfc_scan_complete_sem);
        break;

      case NFC_EVENT_TAG_LOST:
        LOG_INF("Tag lost at %u ms", event.timestamp);
        break;

      case NFC_EVENT_ERROR:
        LOG_ERR("NFC error occurred");
        k_sem_give(&nfc_scan_complete_sem);
        break;
      }
    }
  }
}

#if NFC_READ_SINGLE_BLOCK_MODE
/*============================================================================
 * SINGLE BLOCK MODE - Production
 * Reads only the custom data block
 *============================================================================*/
static void process_tag_detected(uint8_t *uid, uint8_t *custom_data,
                                 uint8_t block_size, uint32_t timestamp) {
  (void)uid;
  (void)timestamp;

  LOG_INF("Block %d data (%d bytes): %02X %02X %02X %02X",
          NFC_CUSTOM_DATA_BLOCK, block_size, custom_data[0], custom_data[1],
          custom_data[2], custom_data[3]);
}

#else
/*============================================================================
 * FULL DUMP MODE - Debugging
 * Reads all blocks and prints a hex dump with ASCII
 *============================================================================*/
static void print_tag_data(uint8_t *data, size_t len, uint8_t block_size) {
  if (len == 0) {
    LOG_INF("No tag data to display");
    return;
  }

  LOG_INF("================ TAG MEMORY DUMP (%d bytes) ================", len);
  LOG_INF("Block | Hex Data                          | ASCII");
  LOG_INF("------+-----------------------------------+------------------");

  /* Print blocks - group by 4 blocks (up to 16 bytes per line) */
  size_t bytes_per_row = block_size * 4; /* Show 4 blocks per row */
  if (bytes_per_row > 16) {
    bytes_per_row = 16;
  }

  for (size_t row = 0; row < len; row += bytes_per_row) {
    size_t blk_start = row / block_size;
    size_t blk_end = (row + bytes_per_row - 1) / block_size;
    if (blk_end >= len / block_size) {
      blk_end = (len / block_size) - 1;
    }

    char hex_str[64] = {0};
    char ascii[17] = {0};
    int hex_pos = 0;

    /* Build hex and ASCII representation */
    for (size_t i = 0; i < bytes_per_row && (row + i) < len; i++) {
      uint8_t c = data[row + i];
      hex_pos +=
          snprintf(&hex_str[hex_pos], sizeof(hex_str) - hex_pos, "%02X ", c);
      if ((i + 1) % block_size == 0 && (row + i + 1) < len) {
        hex_pos += snprintf(&hex_str[hex_pos], sizeof(hex_str) - hex_pos, " ");
      }
      ascii[i] = (c >= 32 && c < 127) ? c : '.';
    }

    LOG_INF("%02d-%02d | %-35s | %s", blk_start, blk_end, hex_str, ascii);
    k_msleep(10); /* Small delay to prevent RTT overflow */
  }

  LOG_INF("=============================================================");
}

static void process_tag_detected(uint8_t *uid, uint8_t *tag_data,
                                 size_t tag_data_len, uint8_t block_size,
                                 uint8_t num_blocks, uint32_t timestamp) {
  (void)uid;
  (void)num_blocks;
  (void)timestamp;

  /* Print full tag data dump */
  if (tag_data_len > 0) {
    print_tag_data(tag_data, tag_data_len, block_size);
  }
}
#endif

static void handle_application_logic(void) {
  /* Main application logic that runs independently of NFC */
  /* Examples: Handle other sensors, network communication, etc. */
  static uint32_t counter = 0;
  counter++;

  if (counter % 10 == 0) {
    LOG_INF("Application running... (counter: %u)", counter);
  }
}

/*============================================================================
 * PN5180 Verification and Info Functions
 *============================================================================*/

/* Print PN5180 version information */
static void print_pn5180_version(const struct pn5180_version_info *info) {
  LOG_INF("=========================================");
  LOG_INF("PN5180 NFC Controller Information:");
  LOG_INF("  Product Version:  %d.%d", (info->product_version >> 8) & 0xFF,
          info->product_version & 0xFF);
  LOG_INF("  Firmware Version: %d.%d", (info->firmware_version >> 8) & 0xFF,
          info->firmware_version & 0xFF);
  LOG_INF("  EEPROM Version:   %d.%d", (info->eeprom_version >> 8) & 0xFF,
          info->eeprom_version & 0xFF);
  LOG_INF("=========================================");
}

/*
 * Verify PN5180 SPI communication by reading EEPROM.
 * This is a good first check after power-on to ensure SPI is working.
 * Returns 0 on success, negative error code on failure.
 */
static int verify_pn5180_communication(void) {
  struct pn5180_version_info version;
  int ret;

  LOG_INF("Verifying PN5180 SPI communication...");

  /* Read version info from EEPROM - this verifies SPI is working */
  ret = pn5180_get_version(pn5180_dev, &version);
  if (ret != 0) {
    LOG_ERR("Failed to read PN5180 version: %s", pn5180_strerror(ret));
    LOG_ERR("SPI communication may not be working!");
    return ret;
  }

  /* Sanity check - firmware version should be non-zero */
  if (version.firmware_version == 0 && version.product_version == 0) {
    LOG_ERR("Invalid version data read - all zeros!");
    LOG_ERR("Check SPI wiring and PN5180 power supply.");
    return -EIO;
  }

  /* Print version info */
  print_pn5180_version(&version);

  LOG_INF("PN5180 SPI communication verified successfully!");
  return 0;
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

/* Power down NFC system - prepare for external power cut */
static void power_down_nfc_system(void) {
  LOG_INF("Powering down NFC system");

  /* Prepare PN5180 for power off (disables RF, clears IRQs, sets idle) */
  int ret = pn5180_prepare_poweroff(pn5180_dev);
  if (ret != 0) {
    LOG_WRN("prepare_poweroff returned: %d (continuing anyway)", ret);
  }

  /*
   * At this point it's safe to cut power to the PN5180.
   * Add your power rail disable calls here, e.g.:
   * power_ctrl_set(POWER_EN_3V3A, false);
   */
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
  ret = pn5180_init(pn5180_dev);
  if (ret != 0) {
    LOG_ERR("Failed to initialize PN5180: %s", pn5180_strerror(ret));
    return -EIO;
  }

  /*
   * Verify SPI communication by reading EEPROM (firmware version).
   * This is a critical first check - if this fails, SPI is not working.
   */
  ret = verify_pn5180_communication();
  if (ret != 0) {
    LOG_ERR("PN5180 communication verification failed!");
    LOG_ERR("Please check: SPI wiring, NSS/CS pin, BUSY pin, power supply");
    return ret;
  }

  /* Configure for ISO15693 protocol */
  ret = pn5180_configure(pn5180_dev, PN5180_PROTOCOL_ISO15693);
  if (ret != 0) {
    LOG_ERR("Failed to configure PN5180: %s", pn5180_strerror(ret));
    return -EIO;
  }
  LOG_INF("PN5180 configured for ISO15693 protocol");

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