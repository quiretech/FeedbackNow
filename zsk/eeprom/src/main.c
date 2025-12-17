/*
 * Copyright (c) 2024 Open Pixel Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "power_ctrl.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define EEPROM_ADDR 0x56
#define EEPROM_PAGE_SIZE 128

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

int eeprom_write_byte(const struct device *i2c, uint8_t addr, uint16_t mem_addr,
                      uint8_t data) {
  uint8_t write_buf[3];

  // 24AA512 uses 16-bit addressing
  write_buf[0] = (mem_addr >> 8) & 0xFF; // High byte of address
  write_buf[1] = mem_addr & 0xFF;        // Low byte of address
  write_buf[2] = data;

  return i2c_write(i2c, write_buf, 3, addr);
}

int eeprom_read_byte(const struct device *i2c, uint8_t addr, uint16_t mem_addr,
                     uint8_t *data) {
  uint8_t addr_buf[2];

  // Set the memory address
  addr_buf[0] = (mem_addr >> 8) & 0xFF; // High byte of address
  addr_buf[1] = mem_addr & 0xFF;        // Low byte of address

  // Write address, then read data
  return i2c_write_read(i2c, addr, addr_buf, 2, data, 1);
}

int eeprom_write_page(const struct device *i2c, uint8_t addr, uint16_t mem_addr,
                      const uint8_t *data, uint8_t len) {
  uint8_t write_buf[2 + EEPROM_PAGE_SIZE];

  if (len > EEPROM_PAGE_SIZE) {
    return -EINVAL;
  }

  // 24AA512 uses 16-bit addressing
  write_buf[0] = (mem_addr >> 8) & 0xFF; // High byte of address
  write_buf[1] = mem_addr & 0xFF;        // Low byte of address

  // Copy data
  memcpy(&write_buf[2], data, len);

  return i2c_write(i2c, write_buf, 2 + len, addr);
}

int eeprom_read_range(const struct device *i2c, uint8_t addr,
                      uint16_t start_addr, uint8_t *data, uint16_t len) {
  uint8_t addr_buf[2];
  int ret;
  uint16_t bytes_read = 0;
  uint16_t chunk_size;

  // Set the starting memory address
  addr_buf[0] = (start_addr >> 8) & 0xFF; // High byte of address
  addr_buf[1] = start_addr & 0xFF;        // Low byte of address

  // Read data in chunks to avoid I2C buffer limitations
  while (bytes_read < len) {
    // Calculate chunk size (read up to 256 bytes at a time)
    chunk_size = (len - bytes_read > 256) ? 256 : (len - bytes_read);

    // Write address, then read data
    ret = i2c_write_read(i2c, addr, addr_buf, 2, &data[bytes_read], chunk_size);
    if (ret != 0) {
      return ret;
    }

    bytes_read += chunk_size;

    // Update address for next chunk
    start_addr += chunk_size;
    addr_buf[0] = (start_addr >> 8) & 0xFF;
    addr_buf[1] = start_addr & 0xFF;
  }

  return 0;
}

int eeprom_read_all(const struct device *i2c, uint8_t addr, uint8_t *data) {
  return eeprom_read_range(i2c, addr, 0x0000, data, 65536);
}

int main(void) {
  uint8_t write_data[] = "Jatan!";
  uint8_t read_data[sizeof(write_data)];
  int ret;

  printk("I2C EEPROM Master Sample\n");

  /* Initialize and enable all power rails first */
  ret = power_ctrl_init();
  if (ret != 0) {
    printk("Failed to initialize power control: %d\n", ret);
    return 0;
  }

  ret = power_ctrl_enable_all();
  if (ret != 0) {
    printk("Failed to enable power rails: %d\n", ret);
    return 0;
  }

  printk("Power rails enabled, continuing with initialization...\n");

  if (!device_is_ready(i2c_dev)) {
    printk("I2C device not ready\n");
    return 0;
  }

  printk("I2C device ready\n");

  // Wait a bit for EEPROM to be ready
  k_msleep(10);

  // Write a single byte
  printk("Writing single byte to address 0x0000...\n");
  ret = eeprom_write_byte(i2c_dev, EEPROM_ADDR, 0x0000, 0xff);
  if (ret != 0) {
    printk("Failed to write byte: %d\n", ret);
  } else {
    printk("Byte written successfully\n");
  }

  // Wait for write to complete
  k_msleep(10);

  // Read the byte back
  printk("Reading byte from address 0x0000...\n");
  ret = eeprom_read_byte(i2c_dev, EEPROM_ADDR, 0x0000, read_data);
  if (ret != 0) {
    printk("Failed to read byte: %d\n", ret);
  } else {
    printk("Read byte: 0x%02X\n", read_data[0]);
  }

  // Write a page of data
  printk("Writing page to address 0x0100...\n");
  ret = eeprom_write_page(i2c_dev, EEPROM_ADDR, 0x0100, write_data,
                          sizeof(write_data));
  if (ret != 0) {
    printk("Failed to write page: %d\n", ret);
  } else {
    printk("Page written successfully\n");
  }

  // Wait for write to complete
  k_msleep(10);

  // Read the page back
  printk("Reading page from address 0x0100...\n");
  ret = eeprom_read_range(i2c_dev, EEPROM_ADDR, 0x0100, read_data,
                          sizeof(write_data));
  if (ret != 0) {
    printk("Failed to read page: %d\n", ret);
  } else {
    printk("Read data: %s\n", read_data);
  }

  printk("EEPROM test completed\n");

  printk("Scanning I2C bus...\n");

  for (uint8_t addr = 0x00; addr <= 0x77; addr++) {
    uint8_t dummy;
    int ret = i2c_read(i2c_dev, &dummy, 1, addr);
    if (ret == 0) {
      printk("Found device at 0x%02X\n", addr);
    }
    k_msleep(10);
  }

  printk("Scan complete.\n");
  return 0;
}
