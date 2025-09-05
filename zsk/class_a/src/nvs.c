
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/console/console.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>

#include "nvs.h"

LOG_MODULE_REGISTER(nvs, LOG_LEVEL_INF);

const int nVars = 4;
const char *nvs_name[] = {"DevNonce", "DevEUI", "JoinEUI", "AppKey"}; // params to store
int nvs_len[] = {2, 8, 8, 16}; // length of each param respectively

static int generate_random_key(uint8_t *buf, size_t len)
{
	int ret = sys_csrand_get(buf, len);
	if (ret < 0) {
		LOG_ERR("Failed to generate random key of size %d\n", len);
		return ret;
	}
	return 0;
}

void nvs_initialize(struct nvs_fs *fs)
{
	int rc;
	struct flash_pages_info info;

	fs->flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(fs->flash_device)) {
		LOG_ERR("Flash device %s is not ready\n", fs->flash_device->name);
		return;
	}
	fs->offset = NVS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs->flash_device, fs->offset, &info);
	if (rc) {
		LOG_ERR("Unable to get page info, rc=%d\n", rc);
		return;
	}
	fs->sector_size = info.size;
	fs->sector_count = 3U;

	rc = nvs_mount(fs);
	if (rc) {
		LOG_ERR("Flash Init failed, rc=%d\n", rc);
		return;
	}
}

void nvs_read_init_parameter(struct nvs_fs *fs, uint16_t id, void *data)
{
	int ret;
	ssize_t bytes_written;
	char *array = (void *)data;
	int *devnonce = (void *)data;

	LOG_INF("\nNVS ID: %d (%s)", id, nvs_name[id]);
	ret = nvs_read(fs, id, data, nvs_len[id]);

	if (ret > 0) {
		// item is found in NVS, lets print it:
		switch (id) {
		case NVS_DEVNONCE_ID:
			LOG_INF("%d \n", (uint16_t)*devnonce);
			break;
		case NVS_LORAWAN_DEV_EUI_ID:
		case NVS_LORAWAN_JOIN_EUI_ID:
		case NVS_LORAWAN_APP_KEY_ID:
			for (int i = 0; i < nvs_len[id]; i++) {
				printk(" %02X ", array[i]);
			}
			printk("\n");
			break;
		default:
			break;
		}
	} else {
		// item not found, let's store keys (generate random 2 x 8byte and 1 x 16 byte, and
		// 2 byte for nonce)
		printk(" not Found on NVS");
		switch (id) {
		case NVS_DEVNONCE_ID:
			*devnonce = 0;
			LOG_INF("Initialized to %u.", *devnonce);
			break;
		case NVS_LORAWAN_DEV_EUI_ID:
		case NVS_LORAWAN_JOIN_EUI_ID:
		case NVS_LORAWAN_APP_KEY_ID: {
			uint8_t rand_key[16]; // max required size
			size_t key_len = nvs_len[id];

			if (generate_random_key(rand_key, key_len) < 0) {
				LOG_ERR("Failed to generate random key for ID %d", id);
				break;
			}

			bytes_written = nvs_write(fs, id, rand_key, key_len);
			if (bytes_written < 0) {
				LOG_ERR("Failed to write key to NVS for ID %d: %d", id, ret);
				break;
			}

			memcpy(data, rand_key, key_len);

			LOG_INF("Generated and stored %s: ", nvs_name[id]);
			for (int i = 0; i < key_len; i++) {
				printk("%02X ", rand_key[i]);
			}
			printk("\n");
			break;
		}
		}
	}
}

// int main(void)
// {
// 	struct nvs_fs fs;
// 	LOG_INF("Starting NVS test app\n");

// 	nvs_initialize(&fs);

// 	for (int i = 0; i < nVars; i++) {
// 		uint8_t data[16] = {0}; // max size buffer for keys (16 bytes)
// 		LOG_INF("Reading NVS variable: %s (ID %d)", nvs_name[i], i);
// 		nvs_read_init_parameter(&fs, i, data);
// 	}

// 	LOG_INF("NVS test complete\n");
// 	return 0;
// }