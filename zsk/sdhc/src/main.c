/*
 * SD Card Hello World Example for nRF52840DK
 * With optional erase/format on boot
 */

#include "power_ctrl.h"

#include <ff.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>

#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT "/SD:"

/* ====================== CONFIG FLAG ======================= */
#define ERASE_SD_ON_BOOT 0 /* Set to 1 to erase card on boot, 0 to disable */
/* =========================================================== */

static FATFS fat_fs;
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = DISK_MOUNT_PT,
};

/* ========== Step 0: Format (Erase) SD Card ========== */
static int format_sd_card(void) {
  printk("\n=== STEP 0: Formatting (Erasing) SD Card ===\n");

  MKFS_PARM opt = {
      .fmt = FM_FAT32, // Format as FAT32
      .n_fat = 1,
      .align = 0,
      .n_root = 0,
      .au_size = 0 // automatic allocation unit
  };

  FRESULT res = f_mkfs(DISK_MOUNT_PT, &opt, NULL, 0);

  if (res != FR_OK) {
    printk("ERROR: Failed to format SD card (FR=%d)\n", res);
    return -1;
  }

  printk("SUCCESS: SD card formatted\n");
  return 0;
}

/* ========== Step 4: List Directory ========== */
static void list_directory(const char *path) {
  struct fs_dir_t dirp;
  struct fs_dirent entry;
  int count = 0;

  fs_dir_t_init(&dirp);

  printk("\n=== STEP 4: Listing SD Card Contents ===\n");

  if (fs_opendir(&dirp, path) != 0) {
    printk("ERROR: Failed to open directory\n");
    return;
  }

  printk("Contents of %s:\n", path);

  while (1) {
    if (fs_readdir(&dirp, &entry) != 0 || entry.name[0] == 0) {
      break;
    }

    if (entry.type == FS_DIR_ENTRY_DIR) {
      printk("  [DIR ] %s\n", entry.name);
    } else {
      printk("  [FILE] %s (%u bytes)\n", entry.name, (unsigned int)entry.size);
    }
    count++;
  }

  fs_closedir(&dirp);
  printk("Total entries: %d\n", count);
}

/* ========== Step 1: Mount SD Card ========== */
static int mount_sd_card(void) {
  int res;

  printk("\n=== STEP 1: Mounting SD Card ===\n");
  printk("Note: SD card must be formatted with FAT filesystem\n");

  res = fs_mount(&mp);
  if (res != 0) {
    printk("ERROR: Failed to mount filesystem (error %d)\n", res);
    return res;
  }

  printk("SUCCESS: SD card mounted\n");
  return 0;
}

/* ========== Step 2: Create File ========== */
static int create_test_file(void) {
  struct fs_file_t file;
  const char *filename = DISK_MOUNT_PT "/hello.txt";
  const char *content = "Hello World from nRF52840DK!\n"
                        "SD card write test successful.\n"
                        "This file was created by the hello world example.\n";

  printk("\n=== STEP 2: Creating File with Content ===\n");

  fs_file_t_init(&file);

  if (fs_open(&file, filename, FS_O_CREATE | FS_O_WRITE) != 0) {
    printk("ERROR: Failed to create file\n");
    return -1;
  }

  ssize_t bytes_written = fs_write(&file, content, strlen(content));
  fs_close(&file);

  if (bytes_written < 0) {
    printk("ERROR: Failed to write to file\n");
    return -1;
  }

  printk("SUCCESS: Created 'hello.txt' with %u bytes\n",
         (unsigned int)bytes_written);
  return 0;
}

/* ========== Step 3: Create Directory ========== */
static int create_test_directory(void) {
  const char *dirname = DISK_MOUNT_PT "/mydata";

  printk("\n=== STEP 3: Creating Directory ===\n");

  int res = fs_mkdir(dirname);
  if (res != 0) {
    printk("ERROR: Failed to create directory (err %d)\n", res);
    return -1;
  }

  printk("SUCCESS: Created directory 'mydata'\n");
  return 0;
}

/* ========== Main Program ========== */
int main(void) {
  int ret;
  uint32_t block_count = 0, block_size = 0;
  uint64_t memory_size_mb;

  printk("\n--- Initializing SD Card ---\n");

  ret = power_ctrl_init();
  if (ret != 0) {
    printk("ERROR: Power control init failed (%d)\n", ret);
    goto error;
  }

  ret = power_ctrl_set(POWER_EN_3V3, true);
  if (ret != 0) {
    printk("ERROR: Failed to enable 3V3 rail (%d)\n", ret);
    goto error;
  }
  ret = power_ctrl_set(POWER_EN_1V8, true);
  if (ret != 0) {
    printk("ERROR: Failed to enable 1V8 rail (%d)\n", ret);
    goto error;
  }
  ret = power_ctrl_set(POWER_EN_3V3A, true);
  if (ret != 0) {
    printk("ERROR: Failed to enable 3V3A rail (%d)\n", ret);
    goto error;
  }
  ret = power_ctrl_set(POWER_EN_3V6, true);
  if (ret != 0) {
    printk("ERROR: Failed to enable 3V6 rail (%d)\n", ret);
    goto error;
  }

  k_sleep(K_MSEC(10));

  if (disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_CTRL_INIT, NULL) != 0) {
    printk("ERROR: SD card init failed!\n");
    goto error;
  }

  if (disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_GET_SECTOR_COUNT,
                        &block_count) != 0) {
    printk("ERROR: Failed to get sector count\n");
    goto error;
  }

  if (disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_GET_SECTOR_SIZE,
                        &block_size) != 0) {
    printk("ERROR: Failed to get sector size\n");
    goto error;
  }

  memory_size_mb = ((uint64_t)block_count * block_size) >> 20;

  printk("SD Card Info:\n");
  printk("  Block count: %u\n", block_count);
  printk("  Sector size: %u bytes\n", block_size);
  printk("  Total size: %u MB\n", (uint32_t)memory_size_mb);

  /* OPTIONAL ERASE / FORMAT PHASE */
#if ERASE_SD_ON_BOOT
  if (format_sd_card() != 0) {
    printk("SD card format failed\n");
    goto error;
  }
#else
  printk("\nErase on boot disabled (ERASE_SD_ON_BOOT = 0)\n");
#endif

  if (mount_sd_card() != 0)
    goto error;
  if (create_test_file() != 0)
    goto cleanup;
  if (create_test_directory() != 0)
    goto cleanup;

  list_directory(DISK_MOUNT_PT);

  printk("\n=====================================\n");
  printk("  All operations completed!\n");
  printk("=====================================\n");

cleanup:
  fs_unmount(&mp);
  disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_CTRL_DEINIT, NULL);

error:
  printk("\nProgram finished. System will now idle.\n");

  while (1) {
    k_sleep(K_MSEC(1000));
  }

  return 0;
}
