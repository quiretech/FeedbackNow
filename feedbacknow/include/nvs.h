#ifndef NVS_H
#define NVS_H

#include "sys_config.h"
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#define NVS_PARTITION storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

// NVS configuration is now in sys_config.h

enum nvs_op {
  NVS_OP_READ,
  NVS_OP_WRITE,
  NVS_OP_INIT,
};

struct nvs_msg {
  enum nvs_op op;
  uint16_t id;
  void *data; // pointer to buffer
  size_t len;
  int *result_ptr;        // new pointer for returning result
  struct k_sem *sync_sem; // pointer to sync semaphore
};

// NVS functions
int nvs_initialize(struct nvs_fs *fs);
int nvs_manager_read(uint16_t id, void *buf, size_t len);
int nvs_manager_write(uint16_t id, const void *buf, size_t len);
int nvs_manager_read_or_generate(uint16_t id, uint8_t *data, size_t size);

#endif
