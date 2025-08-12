#ifndef NVS_H
#define NVS_H

#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#define NVS_PARTITION storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

#define NVS_DEVNONCE_ID 0
#define NVS_LORAWAN_DEV_EUI_ID 1
#define NVS_LORAWAN_JOIN_EUI_ID 2
#define NVS_LORAWAN_APP_KEY_ID 3

#define NVS_MSGQ_MAX_MSGS 10

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

#endif
