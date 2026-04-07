#include "sdcard_logger.h"

#include "power_rail_mgr.h"
#include "spi_mutex.h"

#include <errno.h>
#include <ff.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(sdcard_logger, LOG_LEVEL_INF);

#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT "/SD:"
#define DOWNLINK_LOG_FILE DISK_MOUNT_PT "/downlink.log"

static FATFS fat_fs;
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = DISK_MOUNT_PT,
};

static struct k_mutex sd_mutex;
static bool sd_ready;
static bool sd_init_once;

struct dl_item {
  uint8_t port;
  uint8_t flags;
  int16_t rssi;
  int8_t snr;
  uint8_t len;
  uint8_t data[255];
};

K_MSGQ_DEFINE(sd_dl_msgq, sizeof(struct dl_item), 4, 4);
static struct k_work sd_work;
static struct k_work_q sd_wq;
K_THREAD_STACK_DEFINE(sd_wq_stack, 2048);

static void sd_work_handler(struct k_work *work);

bool sdcard_logger_is_ready(void) { return sd_ready; }

int sdcard_logger_init(void) {
  if (sd_init_once) {
    return sd_ready ? 0 : -EIO;
  }
  sd_init_once = true;

  k_mutex_init(&sd_mutex);
  k_work_init(&sd_work, sd_work_handler);

  /* Dedicated workqueue so SD writes can block waiting for 3V3A without
   * starving the system workqueue.
   */
  k_work_queue_start(&sd_wq, sd_wq_stack, K_THREAD_STACK_SIZEOF(sd_wq_stack), 5,
                     NULL);

  /* IMPORTANT:
   * LoRa requires 3V3A OFF, but SD needs 3V3A ON.
   * If we mount once at boot and later 3V3A turns OFF, the card gets power
   * cycled and subsequent reads/writes can fail with -EIO (-5).
   *
   * So we only mark the logger "ready" (queue + worker available) here.
   * Each write does: 3V3A ON → disk init → mount → append → unmount → deinit
   * → 3V3A OFF.
   */
  sd_ready = true;
  LOG_INF("SD logger initialized (lazy init/mount per write)");
  return 0;
}

static int append_bytes(const void *buf, size_t len) {
  struct fs_file_t file;
  fs_file_t_init(&file);

  int ret =
      fs_open(&file, DOWNLINK_LOG_FILE, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
  if (ret != 0) {
    return ret;
  }

  ssize_t wr = fs_write(&file, buf, len);
  (void)fs_close(&file);

  if (wr < 0) {
    return (int)wr;
  }
  if ((size_t)wr != len) {
    return -EIO;
  }
  return 0;
}

static void sd_work_handler(struct k_work *work) {
  ARG_UNUSED(work);

  if (!sd_ready) {
    return;
  }

  struct dl_item item;
  while (k_msgq_get(&sd_dl_msgq, &item, K_NO_WAIT) == 0) {
    /* Full per-write lifecycle to survive 3V3A power-cycling. */
    int ret = 0;

    /* SD shares SPI with EPD; serialize access so only one uses the bus. */
    if (spi_mutex_lock(K_FOREVER) != 0) {
      LOG_WRN("SD worker: SPI mutex lock failed");
      continue;
    }

    (void)power_rail_mgr_require_3v3a_on(POWER_RAIL_CLIENT_SD, K_FOREVER);

    ret = disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_CTRL_INIT, NULL);
    if (ret != 0) {
      LOG_WRN("SD disk init failed: %d", ret);
      goto out_power;
    }

    ret = fs_mount(&mp);
    if (ret != 0) {
      LOG_WRN("SD mount failed: %d", ret);
      goto out_deinit;
    }

    ret = sdcard_logger_append_downlink(item.port, item.flags, item.rssi,
                                        item.snr, item.data, item.len);

    (void)fs_unmount(&mp);

  out_deinit:
    (void)disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_CTRL_DEINIT, NULL);
  out_power:
    power_rail_mgr_release_3v3a_on(POWER_RAIL_CLIENT_SD);
    spi_mutex_unlock();

    if (ret != 0) {
      LOG_WRN("SD worker append failed: %d", ret);
      /* Keep sd_ready=true so we can retry on next downlink. */
    }
  }
}

int sdcard_logger_append_downlink(uint8_t port, uint8_t flags, int16_t rssi,
                                  int8_t snr, const uint8_t *data,
                                  uint8_t len) {
  if (!sd_ready || data == NULL || len == 0) {
    return -ENODEV;
  }

  /* Keep it simple for the test: one line per downlink */
  k_mutex_lock(&sd_mutex, K_FOREVER);

  char hdr[96];
  int hdr_len = snprintk(hdr, sizeof(hdr),
                         "%lld,port=%u,flags=0x%02x,rssi=%d,snr=%d,len=%u,hex=",
                         k_uptime_get(), port, flags, rssi, snr, len);
  if (hdr_len < 0) {
    k_mutex_unlock(&sd_mutex);
    return -EINVAL;
  }

  int ret = append_bytes(hdr, (size_t)MIN(hdr_len, (int)sizeof(hdr) - 1));
  if (ret != 0) {
    k_mutex_unlock(&sd_mutex);
    return ret;
  }

  for (uint8_t i = 0; i < len; i++) {
    char b[2];
    /* Write uppercase hex without allocating a big buffer */
    static const char hex[] = "0123456789ABCDEF";
    b[0] = hex[(data[i] >> 4) & 0x0F];
    b[1] = hex[data[i] & 0x0F];
    ret = append_bytes(b, sizeof(b));
    if (ret != 0) {
      k_mutex_unlock(&sd_mutex);
      return ret;
    }
  }

  ret = append_bytes("\n", 1);
  k_mutex_unlock(&sd_mutex);
  return ret;
}

int sdcard_logger_submit_downlink(uint8_t port, uint8_t flags, int16_t rssi,
                                  int8_t snr, const uint8_t *data,
                                  uint8_t len) {
  if (!sd_ready || data == NULL || len == 0) {
    return -ENODEV;
  }

  struct dl_item item = {
      .port = port,
      .flags = flags,
      .rssi = rssi,
      .snr = snr,
      .len = len,
  };

  if (item.len > sizeof(item.data)) {
    item.len = sizeof(item.data);
  }
  memcpy(item.data, data, item.len);

  int ret = k_msgq_put(&sd_dl_msgq, &item, K_NO_WAIT);
  if (ret != 0) {
    return ret;
  }

  /* Process ASAP in SD workqueue context */
  (void)k_work_submit_to_queue(&sd_wq, &sd_work);
  return 0;
}
