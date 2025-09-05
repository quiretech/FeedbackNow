// /*
//  * Copyright (c) 2016 Open-RnD Sp. z o.o.
//  * Copyright (c) 2020 Nordic Semiconductor ASA
//  *
//  * SPDX-License-Identifier: Apache-2.0
//  *
//  * NOTE: If you are looking into an implementation of button events with
//  * debouncing, check out `input` subsystem and `samples/subsys/input/input_dump`
//  * example instead.
//  */

// #include <zephyr/kernel.h>
// #include <zephyr/device.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/sys/util.h>
// #include <zephyr/sys/printk.h>
// #include <inttypes.h>

// #define SLEEP_TIME_MS 1

// /*
//  * Get button configuration from the devicetree sw0 alias. This is mandatory.
//  */
// #define SW0_NODE DT_ALIAS(sw0)
// #if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
// #error "Unsupported board: sw0 devicetree alias is not defined"
// #endif
// static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
// static struct gpio_callback button_cb_data;

// /*
//  * The led0 devicetree alias is optional. If present, we'll use it
//  * to turn on the LED whenever the button is pressed.
//  */
// static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

// void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
// {
// 	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
// }

// int main(void)
// {
// 	int ret;

// 	if (!gpio_is_ready_dt(&button)) {
// 		printk("Error: button device %s is not ready\n", button.port->name);
// 		return 0;
// 	}

// 	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
// 	if (ret != 0) {
// 		printk("Error %d: failed to configure %s pin %d\n", ret, button.port->name,
// 		       button.pin);
// 		return 0;
// 	}

// 	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
// 	if (ret != 0) {
// 		printk("Error %d: failed to configure interrupt on %s pin %d\n", ret,
// 		       button.port->name, button.pin);
// 		return 0;
// 	}

// 	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
// 	gpio_add_callback(button.port, &button_cb_data);
// 	printk("Set up button at %s pin %d\n", button.port->name, button.pin);

// 	if (led.port && !gpio_is_ready_dt(&led)) {
// 		printk("Error %d: LED device %s is not ready; ignoring it\n", ret, led.port->name);
// 		led.port = NULL;
// 	}
// 	if (led.port) {
// 		ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
// 		if (ret != 0) {
// 			printk("Error %d: failed to configure LED device %s pin %d\n", ret,
// 			       led.port->name, led.pin);
// 			led.port = NULL;
// 		} else {
// 			printk("Set up LED at %s pin %d\n", led.port->name, led.pin);
// 		}
// 	}

// 	printk("Press the button\n");
// 	if (led.port) {
// 		while (1) {
// 			/* If we have an LED, match its state to the button's. */
// 			int val = gpio_pin_get_dt(&button);

// 			if (val >= 0) {
// 				gpio_pin_set_dt(&led, val);
// 			}
// 			k_msleep(SLEEP_TIME_MS);
// 		}
// 	}
// 	return 0;
// }

// #include <zephyr/kernel.h>
// #include <zephyr/device.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/sys/util.h>
// #include <zephyr/sys/printk.h>
// #include <inttypes.h>

// #define SLEEP_TIME_MS 1

// #define SW0_NODE       DT_ALIAS(sw0)
// #define SW1_NODE       DT_ALIAS(sw1)
// #define SW2_NODE       DT_ALIAS(sw2)
// #define SW3_NODE       DT_ALIAS(sw3)
// #define BUTTON_28_NODE DT_NODELABEL(button_28)

// #if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
// #error "Unsupported board: sw0 devicetree alias is not defined"
// #endif

// static const struct gpio_dt_spec button[] = {
// 	GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0}), GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0}),
// 	GPIO_DT_SPEC_GET_OR(SW2_NODE, gpios, {0}), GPIO_DT_SPEC_GET_OR(SW3_NODE, gpios, {0}),
// 	GPIO_DT_SPEC_GET_OR(BUTTON_28_NODE, gpios, {0})};

// static struct gpio_callback button_cb_data[5];

// /*
//  * The led0 devicetree alias is optional. If present, we'll use it
//  * to turn on the LED whenever the button is pressed.
//  */
// static struct gpio_dt_spec led[4] = {
// 	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0}),
// 	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0}),
// 	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0}),
// 	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led3), gpios, {0}),
// };

// void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
// {
// 	for (int i = 0; i < 5; i++) {
// 		if (dev == button[i].port && (pins & BIT(button[i].pin))) {
// 			printk("Button on pin %d pressed at %" PRIu32 "\n", button[i].pin,
// 			       k_cycle_get_32());
// 		}
// 	}
// }

// int main(void)
// {
// 	int ret;

// 	for (int i = 0; i < 5; i++) {
// 		if (!gpio_is_ready_dt(&button[i])) {
// 			printk("Error: button %d device %s is not ready\n", i,
// 			       button[i].port->name);
// 			return 0;
// 		}

// 		ret = gpio_pin_configure_dt(&button[i], GPIO_INPUT);
// 		if (ret != 0) {
// 			printk("Error %d: failed to configure button %d pin %d\n", ret, i,
// 			       button[i].pin);
// 			return 0;
// 		}

// 		ret = gpio_pin_interrupt_configure_dt(&button[i], GPIO_INT_EDGE_TO_ACTIVE);
// 		if (ret != 0) {
// 			printk("Error %d: failed to configure interrupt on button %d pin %d\n", ret,
// 			       i, button[i].pin);
// 			return 0;
// 		}

// 		gpio_init_callback(&button_cb_data[i], button_pressed, BIT(button[i].pin));
// 		gpio_add_callback(button[i].port, &button_cb_data[i]);
// 		printk("Set up button %d at %s pin %d\n", i, button[i].port->name, button[i].pin);
// 	}

// 	for (int i = 0; i < 4; i++) {
// 		if (led[i].port && !gpio_is_ready_dt(&led[i])) {
// 			printk("Error %d: LED %d device %s is not ready; ignoring it\n", ret, i,
// 			       led[i].port->name);
// 			led[i].port = NULL;
// 		}

// 		if (led[i].port) {
// 			ret = gpio_pin_configure_dt(&led[i], GPIO_OUTPUT);
// 			if (ret != 0) {
// 				printk("Error %d: failed to configure LED %d device %s pin %d\n",
// 				       ret, i, led[i].port->name, led[i].pin);
// 				led[i].port = NULL;
// 			} else {
// 				printk("Set up LED %d at %s pin %d\n", i, led[i].port->name,
// 				       led[i].pin);
// 				gpio_pin_set_dt(&led[i], 0);
// 			}
// 		}
// 	}

// 	printk("Press the buttons\n");

// 	while (1) {
// 		for (int i = 0; i < 5; i++) {

// 			int val = gpio_pin_get_dt(&button[i]);
// 			if (val >= 0) {

// 				gpio_pin_set_dt(&led[i], val);
// 			}
// 		}

// 		k_msleep(SLEEP_TIME_MS);
// 	}

// 	return 0;
// }

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

#define SLEEP_TIME_MS 1

/* Device tree nodes for buttons */
#define BUTTON_5_NODE DT_NODELABEL(button5)

#define SW0_NODE DT_ALIAS(sw0)
#define SW1_NODE DT_ALIAS(sw1)
#define SW2_NODE DT_ALIAS(sw2)
#define SW3_NODE DT_ALIAS(sw3)

#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

/* GPIO spec for buttons (configure number of buttons correctly) */
static const struct gpio_dt_spec button[5] = {
	GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0}), GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0}),
	GPIO_DT_SPEC_GET_OR(SW2_NODE, gpios, {0}), GPIO_DT_SPEC_GET_OR(SW3_NODE, gpios, {0}),
	GPIO_DT_SPEC_GET_OR(BUTTON_5_NODE, gpios, {0})};

static struct gpio_callback button_cb_data[5]; /* Array for button callbacks */

/* GPIO spec for LEDs */
static struct gpio_dt_spec led[4] = {
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led3), gpios, {0}),
};

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	for (int i = 0; i < 5; i++) {
		if (dev == button[i].port && (pins & BIT(button[i].pin))) {
			printk("Button on pin %d pressed at %" PRIu32 "\n", button[i].pin,
			       k_cycle_get_32());
		}
	}
}

int main(void)
{
	int ret;

	/* Set up buttons */
	for (int i = 0; i < 5; i++) {
		if (!gpio_is_ready_dt(&button[i])) {
			printk("Error: button %d device %s is not ready\n", i,
			       button[i].port->name);
			return 0;
		}

		ret = gpio_pin_configure_dt(
			&button[i],
			GPIO_INPUT | GPIO_PULL_UP); /* Set pull-up for active low button */
		if (ret != 0) {
			printk("Error %d: failed to configure button %d pin %d\n", ret, i,
			       button[i].pin);
			return 0;
		}

		ret = gpio_pin_interrupt_configure_dt(
			&button[i], GPIO_INT_EDGE_TO_ACTIVE); /* Edge trigger for button press */
		if (ret != 0) {
			printk("Error %d: failed to configure interrupt on button %d pin %d\n", ret,
			       i, button[i].pin);
			return 0;
		}

		gpio_init_callback(&button_cb_data[i], button_pressed, BIT(button[i].pin));
		gpio_add_callback(button[i].port, &button_cb_data[i]);
		printk("Set up button %d at %s pin %d\n", i, button[i].port->name, button[i].pin);
	}

	/* Set up LEDs */
	for (int i = 0; i < 4; i++) {
		if (led[i].port && !gpio_is_ready_dt(&led[i])) {
			printk("Error %d: LED %d device %s is not ready; ignoring it\n", ret, i,
			       led[i].port->name);
			led[i].port = NULL;
		}

		if (led[i].port) {
			ret = gpio_pin_configure_dt(&led[i], GPIO_OUTPUT);
			if (ret != 0) {
				printk("Error %d: failed to configure LED %d device %s pin %d\n",
				       ret, i, led[i].port->name, led[i].pin);
				led[i].port = NULL;
			} else {
				printk("Set up LED %d at %s pin %d\n", i, led[i].port->name,
				       led[i].pin);
				gpio_pin_set_dt(&led[i], 0); /* Set initial LED state to off */
			}
		}
	}

	printk("Press the buttons\n");

	while (1) {
		for (int i = 0; i < 4; i++) {
			int val = gpio_pin_get_dt(&button[i]);
			if (val >= 0) {
				gpio_pin_set_dt(&led[i], val); /* Set LED based on button state */
			}
		}

		k_msleep(SLEEP_TIME_MS);
	}

	return 0;
}
