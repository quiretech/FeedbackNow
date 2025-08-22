// // // #include <zephyr/device.h>
// // // #include <zephyr/drivers/gpio.h>
// // // #include <zephyr/drivers/spi.h>
// // // #include <zephyr/kernel.h>
// // // #include <zephyr/logging/log.h>

// // // LOG_MODULE_REGISTER(epd_gpio, LOG_LEVEL_INF);

// // // #define LED0_NODE DT_ALIAS(led0)
// // // #define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
// // // #define MY_SPI_MASTER DT_NODELABEL(my_spi_master)
// // // #define MY_SPI_MASTER_CS_DT_SPEC \
// // //   SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(reg_my_spi_master))

// // // #define MY_SPI_SLAVE DT_NODELABEL(my_spi_slave)

// // // // SPI master functionality
// // // const struct device *spi_dev;
// // // static struct k_poll_signal spi_done_sig =
// // //     K_POLL_SIGNAL_INITIALIZER(spi_done_sig);

// // // static void spi_init(void) {
// // //   spi_dev = DEVICE_DT_GET(MY_SPI_MASTER);
// // //   if (!device_is_ready(spi_dev)) {
// // //     printk("SPI master device not ready!\n");
// // //   }
// // //   struct gpio_dt_spec spim_cs_gpio = MY_SPI_MASTER_CS_DT_SPEC;
// // //   if (!device_is_ready(spim_cs_gpio.port)) {
// // //     printk("SPI master chip select device not ready!\n");
// // //   }
// // // }

// // // static struct spi_config spi_cfg = {
// // //     .operation =
// // //         SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL |
// // // SPI_MODE_CPHA,
// // //     .frequency = 4000000,
// // //     .slave = 0,
// // //     .cs = {.gpio = MY_SPI_MASTER_CS_DT_SPEC, .delay = 0},
// // // };

// // // static int spi_write_test_msg(void) {
// // //   static uint8_t counter = 0;
// // //   static uint8_t tx_buffer[2];
// // //   static uint8_t rx_buffer[2];

// // //   const struct spi_buf tx_buf = {.buf = tx_buffer, .len =
// // //     sizeof(tx_buffer)
// // // }
// // // ;
// // //   const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};

// // //   struct spi_buf rx_buf = {
// // //       .buf = rx_buffer,
// // //       .len = sizeof(rx_buffer),
// // //   };
// // //   const struct spi_buf_set rx = {.buffers = &rx_buf, .count = 1};

// // //   // Update the TX buffer with a rolling counter
// // //   tx_buffer[0] = counter++;
// // //   printk("SPI TX: 0x%.2x, 0x%.2x\n", tx_buffer[0], tx_buffer[1]);

// // //   // Reset signal
// // //   k_poll_signal_reset(&spi_done_sig);

// // //   // Start transaction
// // //   int error = spi_transceive_signal(spi_dev, &spi_cfg, &tx, &rx,
// // //   &spi_done_sig); if (error != 0) {
// // //     printk("SPI transceive error: %i\n", error);
// // //     return error;
// // //   }

// // //   // Wait for the done signal to be raised and log the rx buffer
// // //   int spi_signaled, spi_result;
// // //   do {
// // //     k_poll_signal_check(&spi_done_sig, &spi_signaled, &spi_result);
// // //   } while (spi_signaled == 0);
// // //   printk("SPI RX: 0x%.2x, 0x%.2x\n", rx_buffer[0], rx_buffer[1]);
// // //   return 0;
// // // }

// // // static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE,
// gpios);

// // // int main(void) {
// // //   int ret;
// // //   if (!device_is_ready(led.port)) {
// // //     return 0;
// // //   }

// // //   ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
// // //   if (ret < 0) {
// // //     return 0;
// // //   }
// // //   spi_init();

// // //   const struct gpio_dt_spec epd_busy =
// // //       GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_busy_gpios);
// // //   const struct gpio_dt_spec epd_dc =
// // //       GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_dc_gpios);
// // //   const struct gpio_dt_spec epd_rst =
// // //       GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_rst_gpios);

// // //   // Check device readiness
// // //   if (!device_is_ready(epd_busy.port)) {
// // //     LOG_ERR("EPD BUSY GPIO device not ready");
// // //     return;
// // //   }
// // //   if (!device_is_ready(epd_dc.port)) {
// // //     LOG_ERR("EPD DC GPIO device not ready");
// // //     return;
// // //   }
// // //   if (!device_is_ready(epd_rst.port)) {
// // //     LOG_ERR("EPD RST GPIO device not ready");
// // //     return;
// // //   }

// // //   // Configure pins
// // //   ret = gpio_pin_configure_dt(&epd_busy, GPIO_OUTPUT_INACTIVE);
// // //   if (ret < 0) {
// // //     LOG_ERR("Failed to configure EPD BUSY pin: %d", ret);
// // //   } else {
// // //     LOG_INF("EPD BUSY pin configured");
// // //     gpio_pin_set_dt(&epd_busy, 1);
// // //     LOG_INF("EPD BUSY pin set HIGH");
// // //   }

// // //   ret = gpio_pin_configure_dt(&epd_dc, GPIO_OUTPUT_INACTIVE);
// // //   if (ret < 0) {
// // //     LOG_ERR("Failed to configure EPD DC pin: %d", ret);
// // //   } else {
// // //     LOG_INF("EPD DC pin configured");
// // //     gpio_pin_set_dt(&epd_dc, 1);
// // //     LOG_INF("EPD DC pin set HIGH");
// // //   }

// // //   ret = gpio_pin_configure_dt(&epd_rst, GPIO_OUTPUT_INACTIVE);
// // //   if (ret < 0) {
// // //     LOG_ERR("Failed to configure EPD RST pin: %d", ret);
// // //   } else {
// // //     LOG_INF("EPD RST pin configured");
// // //     gpio_pin_set_dt(&epd_rst, 1);
// // //     LOG_INF("EPD RST pin set HIGH");
// // //   }

// // //   LOG_INF("EPD GPIO initialization complete");

// // //   while (1) {
// // //     k_msleep(1000);
// // //   }
// // //   return 0;
// // // }

// // #include <zephyr/device.h>
// // #include <zephyr/drivers/gpio.h>
// // #include <zephyr/drivers/spi.h>
// // #include <zephyr/kernel.h>
// // #include <zephyr/logging/log.h>

// // LOG_MODULE_REGISTER(epd_gpio, LOG_LEVEL_INF);

// // #define LED0_NODE DT_ALIAS(led0)
// // #define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
// // #define MY_SPI_MASTER DT_NODELABEL(my_spi_master)
// // #define MY_SPI_MASTER_CS_DT_SPEC \
// //   SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(reg_my_spi_master))

// // #define MY_SPI_SLAVE DT_NODELABEL(my_spi_slave)

// // // SPI master functionality
// // const struct device *spi_dev;
// // static struct k_poll_signal spi_done_sig =
// //     K_POLL_SIGNAL_INITIALIZER(spi_done_sig);

// // static void spi_init(void) {
// //   spi_dev = DEVICE_DT_GET(MY_SPI_MASTER);
// //   if (!device_is_ready(spi_dev)) {
// //     LOG_ERR("SPI master device not ready!");
// //   } else {
// //     LOG_INF("SPI master device ready");
// //   }

// //   struct gpio_dt_spec spim_cs_gpio = MY_SPI_MASTER_CS_DT_SPEC;
// //   if (!device_is_ready(spim_cs_gpio.port)) {
// //     LOG_ERR("SPI master chip select device not ready!");
// //   } else {
// //     LOG_INF("SPI CS GPIO ready (port %p, pin %d)", spim_cs_gpio.port,
// //             spim_cs_gpio.pin);
// //   }
// // }

// // static struct spi_config spi_cfg = {
// //     .operation =
// //         SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL |
// SPI_MODE_CPHA,
// //     .frequency = 4000000,
// //     .slave = 0,
// //     .cs = {.gpio = MY_SPI_MASTER_CS_DT_SPEC, .delay = 0},
// // };

// // static int spi_write_test_msg(void) {
// //   static uint8_t counter = 20;
// //   static uint8_t tx_buffer[2];
// //   static uint8_t rx_buffer[2];

// //   const struct spi_buf tx_buf = {.buf = tx_buffer, .len =
// sizeof(tx_buffer)};
// //   const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};

// //   struct spi_buf rx_buf = {
// //       .buf = rx_buffer,
// //       .len = sizeof(rx_buffer),
// //   };
// //   const struct spi_buf_set rx = {.buffers = &rx_buf, .count = 1};

// //   // Update TX buffer with rolling counter
// //   tx_buffer[0] = counter++;
// //   LOG_INF("SPI TX: 0x%.2x 0x%.2x", tx_buffer[0], tx_buffer[1]);

// //   k_poll_signal_reset(&spi_done_sig);

// //   int error = spi_transceive_signal(spi_dev, &spi_cfg, &tx, &rx,
// //   &spi_done_sig); if (error != 0) {
// //     LOG_ERR("SPI transceive_signal failed: %d", error);
// //     return error;
// //   }

// //   // Wait for signal
// //   int spi_signaled, spi_result;
// //   do {
// //     k_poll_signal_check(&spi_done_sig, &spi_signaled, &spi_result);
// //   } while (spi_signaled == 0);

// //   LOG_INF("SPI RX: 0x%.2x 0x%.2x", rx_buffer[0], rx_buffer[1]);
// //   return 0;
// // }

// // static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

// // const struct gpio_dt_spec epd_busy =
// //     GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_busy_gpios);
// // const struct gpio_dt_spec epd_dc =
// //     GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_dc_gpios);
// // const struct gpio_dt_spec epd_rst =
// //     GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_rst_gpios);

// // // Check device readiness
// // // if (!device_is_ready(epd_busy.port)) {
// // //   LOG_ERR("EPD BUSY GPIO device not ready");
// // //   return 0;
// // // }
// // // if (!device_is_ready(epd_dc.port)) {
// // //   LOG_ERR("EPD DC GPIO device not ready");
// // //   return 0;
// // // }
// // // if (!device_is_ready(epd_rst.port)) {
// // //   LOG_ERR("EPD RST GPIO device not ready");
// // //   return 0;
// // // }
// // // int ret = 0;
// // // // Configure pins
// // // ret = gpio_pin_configure_dt(&epd_busy, GPIO_INPUT);
// // // if (ret < 0) {
// // //   LOG_ERR("Failed to configure EPD BUSY pin: %d", ret);
// // // } else {
// // //   LOG_INF("EPD BUSY pin configured");
// // //   gpio_pin_set_dt(&epd_busy, 1);
// // //   LOG_INF("EPD BUSY pin set HIGH");
// // // }

// // // ret = gpio_pin_configure_dt(&epd_dc, GPIO_OUTPUT_INACTIVE);
// // // if (ret < 0) {
// // //   LOG_ERR("Failed to configure EPD DC pin: %d", ret);
// // // } else {
// // //   LOG_INF("EPD DC pin configured");
// // //   gpio_pin_set_dt(&epd_dc, 1);
// // //   LOG_INF("EPD DC pin set HIGH");
// // // }

// // // ret = gpio_pin_configure_dt(&epd_rst, GPIO_OUTPUT_INACTIVE);
// // // if (ret < 0) {
// // //   LOG_ERR("Failed to configure EPD RST pin: %d", ret);
// // // } else {
// // //   LOG_INF("EPD RST pin configured");
// // //   gpio_pin_set_dt(&epd_rst, 1);
// // //   LOG_INF("EPD RST pin set HIGH");
// // // }

// // static void epd_send_command(uint8_t cmd) {
// //   gpio_pin_set_dt(&epd_dc, 0); // command mode
// //   struct spi_buf buf = {.buf = &cmd, .len = 1};
// //   struct spi_buf_set tx = {.buffers = &buf, .count = 1};
// //   spi_write(spi_dev, &spi_cfg, &tx);
// // }

// // static void epd_send_data(uint8_t data) {
// //   gpio_pin_set_dt(&epd_dc, 1); // data mode
// //   struct spi_buf buf = {.buf = &data, .len = 1};
// //   struct spi_buf_set tx = {.buffers = &buf, .count = 1};
// //   spi_write(spi_dev, &spi_cfg, &tx);
// // }
// // static void epd_wait_busy(void) {
// //   LOG_INF("Waiting for EPD BUSY...");
// //   while (gpio_pin_get_dt(&epd_busy)) {
// //     k_msleep(10);
// //   }
// //   LOG_INF("BUSY=%d in wait busy", gpio_pin_get_dt(&epd_busy));
// //   LOG_INF("EPD ready (BUSY=LOW)");
// // }

// // static void epd_hw_reset(void) {
// //   gpio_pin_set_dt(&epd_rst, 0);
// //   k_msleep(10); // at least 10ms
// //   gpio_pin_set_dt(&epd_rst, 1);
// //   k_msleep(10); // at least 2ms
// //   LOG_INF("HW Reset done");
// // }
// // static void epd_init_ssd1683(void) {
// //   LOG_INF("EPD Init (SSD1683)");

// //   // HW Reset with pulse
// //   epd_hw_reset();
// //   epd_wait_busy();

// //   // SW Reset
// //   epd_send_command(0x12); // SWRESET
// //   k_msleep(10);
// //   epd_wait_busy();

// //   // Driver output control
// //   epd_send_command(0x01); // Driver Output Control
// //   epd_send_data(0x2B);    // A[7:0] = 0x2B
// //   epd_send_data(0x01);    // A[8] = 1
// //   epd_send_data(0x00);    // B[2:0] = 000

// //   // Data entry mode
// //   epd_send_command(0x11);
// //   epd_send_data(0x03); // X increment, Y increment

// //   // Set RAM X address
// //   epd_send_command(0x44);
// //   epd_send_data(0x00); // start 0
// //   epd_send_data(0x31); //

// //   // Set RAM Y address
// //   epd_send_command(0x45);
// //   epd_send_data(0x00);
// //   epd_send_data(0x00);
// //   epd_send_data(0x2B);
// //   epd_send_data(0x01);

// //   // Border waveform
// //   epd_send_command(0x3C);
// //   epd_send_data(0xC0);

// //   /*4. Load Waveform LUT
// //   • Sense temperature by int/ext TS by Command 0x18

// //   • Load waveform LUT from OTP by Command 0x22,
// //   0x20 or by MCU
// //   • Wait BUSY Low*/

// //   // sense temp selection
// //   epd_send_command(0x18);
// //   epd_send_data(0x80); // internal
// //   epd_send_command(0x22);
// //   epd_send_data(0xF7); // clk+analog+load temp + LUT (3-color)
// //   epd_send_command(0x20);
// //   epd_wait_busy();

// //   // load waveform from OTP

// //   /*
// //   5. Write Image and Drive Display Panel
// // • Write image data in RAM by Command 0x4E, 0x4F,
// // 0x24, 0x26
// // • Set softstart setting by Command 0x0C
// // • Drive display panel by Command 0x22, 0x20
// // • Wait BUSY Low*/

// //   // epd_send_command(0x24); // Write BW RAM
// //   // for (int i = 0; i < 50 * 300; i++) {
// //   //   epd_send_data(0x00); // Fill white
// //   // }

// //   // k_msleep(500);

// //   // epd_send_command(0x26);
// //   // for (int i = 0; i < 50 * 300; i++) {
// //   //   epd_send_data(0xFF); // Full red
// //   // }

// //   // k_msleep(500);

// //   // Set softstart setting by Command 0x0C
// //   // epd_send_command(0x0C);
// //   // epd_send_data(0xD7);
// //   // epd_send_data(0xD6);
// //   // epd_send_data(0x9D);

// //   int bytes_per_line = 400 / 8;
// //   int total_bytes = bytes_per_line * 300;

// //   // Clear BW RAM (white = 0xFF)
// //   epd_send_command(0x24);
// //   for (int i = 0; i < total_bytes; i++) {
// //     epd_send_data(0xFF);
// //   }

// //   // Clear RED RAM (no red = 0x00)
// //   epd_send_command(0x26);
// //   for (int i = 0; i < total_bytes; i++) {
// //     epd_send_data(0x00);
// //   }

// //   // Now update once
// //   epd_send_command(0x22);
// //   epd_send_data(0xC7);
// //   epd_send_command(0x20);
// //   epd_wait_busy();

// //   epd_send_command(0x24); // Write BW RAM

// //   int cx = 200; // center x
// //   int cy = 150; // center y
// //   int r = 50;   // radius

// //   epd_send_command(0x24); // Write BW RAM
// //   for (int y = 0; y < 300; y++) {
// //     for (int bx = 0; bx < bytes_per_line; bx++) {
// //       uint8_t data = 0xFF; // default = all white
// //       int x_start = bx * 8;

// //       for (int b = 0; b < 8; b++) {
// //         int x = x_start + b;

// //         // check circle equation
// //         int dx = x - cx;
// //         int dy = y - cy;
// //         if (dx * dx + dy * dy <= r * r) {
// //           data &= ~(1 << (7 - b)); // bit=0 → black pixel
// //         }
// //       }
// //       epd_send_data(data);
// //     }
// //   }

// //   // Optional: clear red RAM so no flecks
// //   epd_send_command(0x26);
// //   for (int i = 0; i < total_bytes; i++) {
// //     epd_send_data(0x00);
// //   }

// //   // Refresh
// //   epd_send_command(0x22);
// //   epd_send_data(0xC7);
// //   epd_send_command(0x20);
// //   epd_wait_busy();

// //   LOG_INF("EPD SSD1683 init complete");
// // }

// // static void epd_init_phase1(void) {
// //   LOG_INF("EPD Init Phase 1: SPI setup + HW/SW reset");
// //   // Configure pins
// //   gpio_pin_configure_dt(&epd_rst, GPIO_OUTPUT_ACTIVE);
// //   gpio_pin_configure_dt(&epd_dc, GPIO_OUTPUT_ACTIVE);
// //   gpio_pin_configure_dt(&epd_busy, GPIO_INPUT);
// // }

// // #include "ssd1683.h"
// // #include <zephyr/kernel.h>

// // int main(void) {
// //   int ret;

// //   // Check LED device
// //   if (!device_is_ready(led.port)) {
// //     LOG_ERR("LED device not ready!");
// //     return 0;
// //   }

// //   ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
// //   if (ret < 0) {
// //     LOG_ERR("Failed to configure LED: %d", ret);
// //     return 0;
// //   } else {
// //     LOG_INF("LED configured and set active");
// //   }

// //   // Check EPD GPIO devices
// //   if (!device_is_ready(epd_busy.port)) {
// //     LOG_ERR("EPD BUSY GPIO device not ready");
// //     return 0;
// //   }
// //   if (!device_is_ready(epd_dc.port)) {
// //     LOG_ERR("EPD DC GPIO device not ready");
// //     return 0;
// //   }
// //   if (!device_is_ready(epd_rst.port)) {
// //     LOG_ERR("EPD RST GPIO device not ready");
// //     return 0;
// //   }

// //   // Init SPI + EPD (Phase 1 reset sequence)
// //   spi_init();
// //   epd_init_phase1(); // handles HW + SW reset
// //   epd_init_ssd1683();
// //   // epd_clear_ssd1683();

// //   while (1) {
// //     ret = gpio_pin_toggle_dt(&led);
// //     if (ret < 0) {
// //       LOG_ERR("Failed to toggle LED: %d", ret);
// //     }
// //     k_msleep(1000);
// //   }

// //   return 0;
// // }
