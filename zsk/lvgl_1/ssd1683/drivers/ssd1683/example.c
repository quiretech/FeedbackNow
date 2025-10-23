// #include "ssd1683.h"
// #include <zephyr/device.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/drivers/spi.h>
// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/sys/printk.h>

// LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// // Device tree node references
// #define ARDUINO_SPI_NODE DT_NODELABEL(arduino_spi)
// #define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

// // Display configuration
// #define DISPLAY_WIDTH SSD1683_WIDTH
// #define DISPLAY_HEIGHT SSD1683_HEIGHT

// // SPI configuration using your arduino_spi
// static const struct spi_dt_spec spi_bus = {
//     .bus = DEVICE_DT_GET(DT_NODELABEL(arduino_spi)),
//     .config = {
//         .operation =
//             SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL |
//             SPI_MODE_CPHA,
//         .frequency = 4000000,
//         .slave = 0,
//         .cs = {.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(arduino_spi), cs_gpios),
//                .delay = 0}}};

// // // GPIO configurations
// static const struct gpio_dt_spec epd_busy =
//     GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_busy_gpios);
// static const struct gpio_dt_spec epd_dc =
//     GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_dc_gpios);
// static const struct gpio_dt_spec epd_rst =
//     GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_rst_gpios);

// // // SSD1683 configuration structure
// static const struct ssd1683_config ssd1683_cfg = {.bus = spi_bus,
//                                                   .dc = epd_dc,
//                                                   .rst = epd_rst,
//                                                   .busy = epd_busy,
//                                                   .width = DISPLAY_WIDTH,
//                                                   .height = DISPLAY_HEIGHT};

// // // SSD1683 device data
// static struct ssd1683_data ssd1683_data;
// // SSD1683 device structure
// static const struct device ssd1683_dev = {
//     .config = &ssd1683_cfg,
//     .data = &ssd1683_data,
// };

// static int demo(void) {
//   int ret;

//   LOG_INF("Starting SSD1683 Hello World Demo");

//   // Initialize the driver
//   ret = ssd1683_init(&ssd1683_dev, &ssd1683_cfg);
//   if (ret < 0) {
//     LOG_ERR("Failed to initialize SSD1683: %d", ret);
//     return ret;
//   }
//   LOG_INF("SSD1683 initialized successfully");

//   // Power on the display
//   ret = ssd1683_power_on(&ssd1683_dev);
//   if (ret < 0) {
//     LOG_ERR("Failed to power on SSD1683: %d", ret);
//     return ret;
//   }
//   LOG_INF("Display powered on");

//   ret = ssd1683_set_fast_update(&ssd1683_dev, false);
//   if (ret < 0) {
//     LOG_ERR("Failed to set fast update: %d", ret);
//     return ret;
//   }
//   LOG_INF("Fast update set to false");

//   ret = ssd1683_clear_screen(&ssd1683_dev, 0xFF);
//   if (ret < 0) {
//     LOG_ERR("Failed to clear screen: %d", ret);
//     return ret;
//   }
//   LOG_INF("Screen cleared to White");
// }

// // Main application function
// int main(void) {
//   int ret;

//   LOG_INF("SSD1683 E-Paper Display Hello World Demo Starting");

//   // Check if all devices are ready
//   if (!spi_is_ready_dt(&spi_bus)) {
//     LOG_ERR("SPI bus not ready");
//     return -ENODEV;
//   }

//   if (!gpio_is_ready_dt(&epd_busy)) {
//     LOG_ERR("BUSY GPIO not ready");
//     return -ENODEV;
//   }

//   if (!gpio_is_ready_dt(&epd_dc)) {
//     LOG_ERR("DC GPIO not ready");
//     return -ENODEV;
//   }

//   if (!gpio_is_ready_dt(&epd_rst)) {
//     LOG_ERR("RST GPIO not ready");
//     return -ENODEV;
//   }

//   ret = demo();
//   if (ret < 0) {
//     LOG_ERR("Hello world demo failed: %d", ret);
//     return ret;
//   }

//   ret = ssd1683_power_off(&ssd1683_dev);
//   if (ret < 0) {
//     LOG_ERR("Failed to power off: %d", ret);
//   } else {
//     LOG_INF("Display powered off");
//   }

//   LOG_INF("Hello World demo completed successfully!");

//   // Main loop - just keep the system running
//   while (1) {
//     k_msleep(10000);
//     LOG_INF("System running...");
//   }
// }