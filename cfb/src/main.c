/*
 * Copyright (c) 2018 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

const struct gpio_dt_spec right_led = GPIO_DT_SPEC_GET(DT_NODELABEL(right_button_led), gpios);
const struct gpio_dt_spec left_led = GPIO_DT_SPEC_GET(DT_NODELABEL(left_button_led), gpios);

int init_led(struct gpio_dt_spec led1)
{
	int ret;

	if (led1.port && !gpio_is_ready_dt(&led1)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, led1.port->name);
		led1.port = NULL;
	}
	if (led1.port) {
		ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed1 to configure LED device %s pin %d\n",
			       ret, led1.port->name, led1.pin);
			led1.port = NULL;
		} else {
			printk("Set up LED at %s pin %d\n", led1.port->name, led1.pin);
		}
	}

	return 0;
}

int main(void)
{
	const struct device *dev;
	uint16_t x_res;
	uint16_t y_res;
	uint16_t rows;
	uint8_t ppt;
	uint8_t font_width;
	uint8_t font_height;
	int ret;

	init_led(left_led);
	init_led(right_led);

	dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(dev)) {
		printf("Device %s not ready\n", dev->name);
		return 0;
	}

	if (display_set_pixel_format(dev, PIXEL_FORMAT_MONO10) != 0) {
		if (display_set_pixel_format(dev, PIXEL_FORMAT_MONO01) != 0) {
			printf("Failed to set required pixel format");
			return 0;
		}
	}

	printf("Initialized %s\n", dev->name);

	if (cfb_framebuffer_init(dev)) {
		printf("Framebuffer initialization failed!\n");
		return 0;
	}

	cfb_framebuffer_clear(dev, true);

	display_blanking_off(dev);

	x_res = cfb_get_display_parameter(dev, CFB_DISPLAY_WIDTH);
	y_res = cfb_get_display_parameter(dev, CFB_DISPLAY_HEIGH);
	rows = cfb_get_display_parameter(dev, CFB_DISPLAY_ROWS);
	ppt = cfb_get_display_parameter(dev, CFB_DISPLAY_PPT);

	for (int idx = 0; idx < 42; idx++) {
		if (cfb_get_font_size(dev, idx, &font_width, &font_height)) {
			break;
		}
		cfb_framebuffer_set_font(dev, idx);
		printf("font width %d, font height %d, %d\n",
		       font_width, font_height, idx);
	}
	cfb_framebuffer_set_font(dev, 0);


	printf("x_res %d, y_res %d, ppt %d, rows %d, cols %d\n",
	       x_res,
	       y_res,
	       ppt,
	       rows,
	       cfb_get_display_parameter(dev, CFB_DISPLAY_COLS));

	cfb_framebuffer_invert(dev);

	cfb_set_kerning(dev, 0);
	cfb_print(dev, "RederoTech", 0, 0);
	cfb_print(dev, "Inside", 0, 18 * 2);
	cfb_invert_area(dev, 0, 18*2, 128, 18*3);
	cfb_framebuffer_finalize(dev);

	k_sleep(K_MSEC(2000));
	display_blanking_on(dev);
	k_sleep(K_MSEC(2000));
	display_blanking_off(dev);
	while (1) {
		ret = gpio_pin_toggle_dt(&right_led);
		if (ret < 0) {
			return 0;
		}
		ret = gpio_pin_toggle_dt(&left_led);
		if (ret < 0) {
			return 0;
		}
		k_sleep(K_MSEC(1000));
	}
	return 0;
}
