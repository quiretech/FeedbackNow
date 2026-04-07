
// #include "ssd1683.h"
// #include <zephyr/device.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/drivers/spi.h>
// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/sys/printk.h>

// #include "AP_29demo.h"

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

// // Helper function to create a filled rectangle bitmap
// static void create_box_bitmap(uint8_t *buffer, int width, int height) {
//   int width_bytes = (width + 7) / 8;

//   for (int y = 0; y < height; y++) {
//     for (int x_byte = 0; x_byte < width_bytes; x_byte++) {
//       int bits_in_byte = (width - x_byte * 8) >= 8 ? 8 : (width - x_byte *
//       8); uint8_t byte_val = 0x00; // Black pixels

//       // Set the appropriate bits
//       for (int bit = 0; bit < bits_in_byte; bit++) {
//         byte_val |= (1 << (7 - bit));
//       }

//       buffer[y * width_bytes + x_byte] = byte_val;
//     }
//   }
// }

// // Helper function to create a white (clear) rectangle bitmap
// static void create_clear_bitmap(uint8_t *buffer, int width, int height) {
//   int width_bytes = (width + 7) / 8;

//   for (int y = 0; y < height; y++) {
//     for (int x_byte = 0; x_byte < width_bytes; x_byte++) {
//       buffer[y * width_bytes + x_byte] = 0xFF; // White pixels
//     }
//   }
// }

// static int demo(void) {
//   int ret;

//   LOG_INF("Starting SSD1683 Animated Box Demo");

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

//   // Enable fast partial updates
//   ret = ssd1683_set_fast_update(&ssd1683_dev, true);
//   if (ret < 0) {
//     LOG_ERR("Failed to set fast update: %d", ret);
//     return ret;
//   }
//   LOG_INF("Fast update enabled");

//   // Clear screen to white with full refresh
//   ret = ssd1683_clear_screen(&ssd1683_dev, 0xFF);
//   if (ret < 0) {
//     LOG_ERR("Failed to clear screen: %d", ret);
//     return ret;
//   }
//   LOG_INF("Screen cleared to White");

// // Define box parameters
// #define BOX_WIDTH 20
// #define BOX_HEIGHT 60
// #define CENTER_X ((DISPLAY_WIDTH - BOX_WIDTH) / 2)
// #define CENTER_Y ((DISPLAY_HEIGHT - BOX_HEIGHT) / 2)
// #define BOX_OFFSET 10 // Distance between boxes

//   // Calculate bitmap buffer size (width must be byte-aligned)
//   int box_width_aligned = ((BOX_WIDTH + 7) / 8) * 8;
//   int buffer_size = ((box_width_aligned + 7) / 8) * BOX_HEIGHT;
//   uint8_t box_bitmap[buffer_size];
//   uint8_t clear_bitmap[buffer_size];

//   // Create box and clear bitmaps
//   create_box_bitmap(box_bitmap, BOX_WIDTH, BOX_HEIGHT);
//   create_clear_bitmap(clear_bitmap, BOX_WIDTH, BOX_HEIGHT);

//   LOG_INF("Starting animation loop (10 cycles)");

//   // Animation loop: move box back and forth
//   for (int cycle = 0; cycle < 2; cycle++) {
//     LOG_INF("Cycle %d/10", cycle + 1);

//     // Position 1: Box at center
//     LOG_INF("Drawing box at position 1 (center)");
//     ret = ssd1683_write_image(&ssd1683_dev, box_bitmap, CENTER_X, CENTER_Y,
//                               BOX_WIDTH, BOX_HEIGHT, false, false);
//     if (ret < 0) {
//       LOG_ERR("Failed to write box at position 1: %d", ret);
//       return ret;
//     }

//     ret = ssd1683_refresh(&ssd1683_dev, true); // Partial refresh
//     if (ret < 0) {
//       LOG_ERR("Failed to refresh at position 1: %d", ret);
//       return ret;
//     }

//     k_msleep(500); // Pause to see the box

//     // Position 2: Box moved 10 pixels to the right
//     LOG_INF("Drawing box at position 2 (10 pixels right)");

//     // First clear the old position
//     ret = ssd1683_write_image(&ssd1683_dev, clear_bitmap, CENTER_X, CENTER_Y,
//                               BOX_WIDTH, BOX_HEIGHT, false, false);
//     if (ret < 0) {
//       LOG_ERR("Failed to clear box at position 1: %d", ret);
//       return ret;
//     }

//     // Draw at new position
//     ret = ssd1683_write_image(&ssd1683_dev, box_bitmap,
//                               CENTER_X + BOX_WIDTH + BOX_OFFSET, CENTER_Y,
//                               BOX_WIDTH, BOX_HEIGHT, false, false);
//     if (ret < 0) {
//       LOG_ERR("Failed to write box at position 2: %d", ret);
//       return ret;
//     }

//     ret = ssd1683_refresh(&ssd1683_dev, true); // Partial refresh
//     if (ret < 0) {
//       LOG_ERR("Failed to refresh at position 2: %d", ret);
//       return ret;
//     }

//     k_msleep(500); // Pause to see the box

//     // Clear position 2 to go back to position 1
//     LOG_INF("Clearing position 2");
//     ret = ssd1683_write_image(&ssd1683_dev, clear_bitmap,
//                               CENTER_X + BOX_WIDTH + BOX_OFFSET, CENTER_Y,
//                               BOX_WIDTH, BOX_HEIGHT, false, false);
//     if (ret < 0) {
//       LOG_ERR("Failed to clear box at position 2: %d", ret);
//       return ret;
//     }

//     // Don't refresh yet - we'll draw position 1 again and refresh together
//   }

//   // After animation, clear the entire screen with full refresh
//   LOG_INF("Animation complete - clearing screen with full refresh");
//   ret = ssd1683_clear_screen(&ssd1683_dev, 0xFF);
//   if (ret < 0) {
//     LOG_ERR("Failed to final clear screen: %d", ret);
//     return ret;
//   }
//   LOG_INF("Demo completed successfully!");

//   ret =
//       ssd1683_write_image(&ssd1683_dev, gImage_1, 0, 0, 400, 300, false,
//       true);
//   if (ret < 0) {

//     return ret;
//   }
//   ret = ssd1683_refresh(&ssd1683_dev, true); // Partial refresh
//   if (ret < 0) {
//     LOG_ERR("Failed to refresh at position 2: %d", ret);
//     return ret;
//   }
//   k_msleep(500); // Pause to see the box

//   ret =
//       ssd1683_write_image(&ssd1683_dev, gImage_2, 0, 0, 400, 300, false,
//       true);
//   if (ret < 0) {

//     return ret;
//   }
//   ret = ssd1683_refresh(&ssd1683_dev, true); // Partial refresh
//   if (ret < 0) {
//     LOG_ERR("Failed to refresh at position 2: %d", ret);
//     return ret;
//   }
//   k_msleep(500); // Pause to see the box

//   ret =
//       ssd1683_write_image(&ssd1683_dev, gImage_p1, 0, 0, 400, 300, false,
//       true);
//   if (ret < 0) {

//     return ret;
//   }
//   ret = ssd1683_refresh(&ssd1683_dev, true); // Partial refresh
//   if (ret < 0) {
//     LOG_ERR("Failed to refresh at position 2: %d", ret);
//     return ret;
//   }
//   k_msleep(500); // Pause to see the box

//   ret =
//       ssd1683_write_image(&ssd1683_dev, gImage_p2, 0, 0, 400, 300, false,
//       true);
//   if (ret < 0) {

//     return ret;
//   }
//   ret = ssd1683_refresh(&ssd1683_dev, true); // Partial refresh
//   if (ret < 0) {
//     LOG_ERR("Failed to refresh at position 2: %d", ret);
//     return ret;
//   }
//   k_msleep(500); // Pause to see the box

//   ret =
//       ssd1683_write_image(&ssd1683_dev, gImage_p3, 0, 0, 400, 300, false,
//       true);
//   if (ret < 0) {

//     return ret;
//   }
//   ret = ssd1683_refresh(&ssd1683_dev, true); // Partial refresh
//   if (ret < 0) {
//     LOG_ERR("Failed to refresh at position 2: %d", ret);
//     return ret;
//   }
//   k_msleep(500); // Pause to see the box

//   return 0;
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

// /* Arduino SPI configuration for EPD and NFC sharing */
// &arduino_spi {
//   status = "okay";
//   cs - gpios = <&gpio1 12 GPIO_ACTIVE_LOW>; /* EPD CS */

// /* EPD device on arduino_spi */
// epd_spi_device:
//   spi - dev - epd @0 {
//     compatible = "spi-device";
//     reg = <0>;
//     spi - max - frequency = <4000000>;
//   };
// };

// / {
//   zephyr, user {
//     epd_busy - gpios = <&gpio0 30 GPIO_ACTIVE_HIGH>;
//     epd_dc - gpios = <&gpio1 11 GPIO_ACTIVE_HIGH>;
//     epd_rst - gpios = <&gpio1 3 GPIO_ACTIVE_HIGH>;
//   };
// };