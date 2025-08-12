#include "k_config.h"
#include "nvs.h"
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(nvs_thread, LOG_LEVEL_INF);

K_MSGQ_DEFINE(nvs_msgq, sizeof(struct nvs_msg), NVS_MSGQ_MAX_MSGS, 4);

static struct nvs_fs fs;

/* helper function */
static int generate_random_key(uint8_t *buf, size_t len) {
  int ret = sys_csrand_get(buf, len);
  if (ret < 0) {
    LOG_ERR("Failed to generate random key of size %d\n", len);
    return ret;
  }
  return 0;
}

static void print_bytes(const char *label, const uint8_t *data, size_t len) {
  char buf[3 * len + 1]; // Each byte as "XX " plus null terminator
  int pos = 0;
  for (size_t i = 0; i < len; i++) {
    pos += snprintf(&buf[pos], sizeof(buf) - pos, "%02X ", data[i]);
  }
  LOG_INF("%s: %s", label, buf);
}

const int nVars = 4;
const char *nvs_name[] = {"DevNonce", "DevEUI", "JoinEUI",
                          "AppKey"}; // params to store
int nvs_len[] = {2, 8, 8, 16};       // length of each param respectively

void nvs_manager_thread(void *arg1, void *arg2, void *arg3) {
  struct nvs_msg msg;

  nvs_initialize(&fs);

  while (1) {
    if (k_msgq_get(&nvs_msgq, &msg, K_FOREVER) == 0) {
      switch (msg.op) {
      case NVS_OP_READ: {
        int res = nvs_read(&fs, msg.id, msg.data, msg.len);
        if (msg.result_ptr) {
          *msg.result_ptr = res;
        }
        break;
      }
      case NVS_OP_WRITE: {
        int res = nvs_write(&fs, msg.id, msg.data, msg.len);
        if (msg.result_ptr) {
          *msg.result_ptr = res;
        }
        break;
      }
      default:
        if (msg.result_ptr) {
          *msg.result_ptr = -EINVAL;
        }
        break;
      }
      k_sem_give(msg.sync_sem);
    }
  }
}

int nvs_manager_read(uint16_t id, void *buf, size_t len) {
  struct k_sem sync_sem;
  int ret_val;
  struct nvs_msg msg = {
      .op = NVS_OP_READ,
      .id = id,
      .data = buf,
      .len = len,
      .result_ptr = &ret_val,
      .sync_sem = &sync_sem,
  };

  k_sem_init(&sync_sem, 0, 1);

  int ret = k_msgq_put(&nvs_msgq, &msg, K_NO_WAIT);
  if (ret < 0) {
    return ret;
  }

  k_sem_take(&sync_sem, K_FOREVER);

  return ret_val; // nvs_read returns bytes read or negative on error
}

int nvs_manager_write(uint16_t id, const void *buf, size_t len) {
  struct k_sem sync_sem;
  int ret_val;
  struct nvs_msg msg = {
      .op = NVS_OP_WRITE,
      .id = id,
      .data = (void *)buf,
      .len = len,
      .result_ptr = &ret_val,
      .sync_sem = &sync_sem,
  };
  k_sem_init(&sync_sem, 0, 1);

  int ret = k_msgq_put(&nvs_msgq, &msg, K_NO_WAIT);
  if (ret < 0) {
    return ret;
  }

  k_sem_take(&sync_sem, K_FOREVER);

  return ret_val; // nvs_write returns 0 on success or negative error
}

int nvs_manager_read_or_generate(uint16_t id, uint8_t *data, size_t size) {
  int ret = nvs_manager_read(id, data, size);
  if (ret >= 0) {
    LOG_INF("Read %s from NVS", nvs_name[id]);
    print_bytes(nvs_name[id], data, size);
    return 0;
  }

  LOG_WRN("%s not found or incomplete in NVS, generating new key...",
          nvs_name[id]);

  uint8_t rand_key[16]; // max size buffer
  if (size > sizeof(rand_key)) {
    LOG_ERR("Size too large for random key buffer");
    return -EINVAL;
  }

  // Generate random key - you need to implement this function using your
  // platform's RNG
  if (generate_random_key(rand_key, size) < 0) {
    LOG_ERR("Failed to generate random key for %s", nvs_name[id]);
    return -EIO;
  }

  // Write generated key to NVS
  ret = nvs_manager_write(id, rand_key, size);
  if (ret < 0) {
    LOG_ERR("Failed to write generated %s to NVS: %d", nvs_name[id], ret);
    return ret;
  }

  // Copy generated key into output buffer
  memcpy(data, rand_key, size);

  LOG_INF("Generated and stored %s:", nvs_name[id]);
  print_bytes(nvs_name[id], data, size);

  return 0;
}

K_THREAD_DEFINE(nvs_thread_id, NVS_THREAD_STACK_SIZE, nvs_manager_thread, NULL,
                NULL, NULL, NVS_THREAD_PRIORITY, 0, 0);

// #define TEST_NVS_ID 4
// #define TEST_STRING "Hllwrld"

// void nvs_test_thread(void *p1, void *p2, void *p3) {
//   int ret;
//   uint8_t read_buf[16] = {0};
//   char buf[16] = {0};

//   k_sleep(K_MSEC(500)); // Ensure NVS thread is ready

//   strcpy(buf, TEST_STRING);

//   ret = nvs_manager_write(TEST_NVS_ID, buf, strlen(buf) + 1);
//   LOG_INF("NVS write returned: %d", ret);
//   if (ret == 0) {
//     LOG_INF("NVS write success");
//   } else {
//     LOG_ERR("NVS write failed: %d", ret);
//   }

//   LOG_INF("Starting NVS read test...");

//   ret = nvs_manager_read(TEST_NVS_ID, read_buf, sizeof(read_buf));
//   LOG_INF("NVS read returned: %d", ret);
//   if (ret >= 0) {
//     LOG_INF("NVS read success, data: %s", read_buf);
//   } else {
//     LOG_WRN("NVS read failed with error: %d", ret);
//   }
// }

// K_THREAD_DEFINE(nvs_test_thread_id, 2048, nvs_test_thread, NULL, NULL, NULL,
// 6,
//                 0, 0);
