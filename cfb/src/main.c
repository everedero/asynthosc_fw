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
#define DISP_NODE DT_NODELABEL(enable_display)

static const struct gpio_dt_spec disp_enable = GPIO_DT_SPEC_GET(DISP_NODE, gpios);

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

	if (!gpio_is_ready_dt(&disp_enable)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&disp_enable, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	ret = gpio_pin_set_dt(&disp_enable, 1);
	if (ret < 0) {
		return 0;
	}

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
	cfb_print(dev, "Welcome to RederoTech", 0, 0);
	cfb_print(dev, "Life is great", 0, 18 * 2);
	cfb_invert_area(dev, 0, 18*2, 128, 18*3);
	cfb_framebuffer_finalize(dev);

/*
	while (1) {
		for (int i = 0; i < MIN(x_res, y_res); i++) {
			cfb_framebuffer_clear(dev, false);
			if (cfb_print(dev,
				      "Welcome to RederoTech\nYo",
				      i, i)) {
				printf("Failed to print a string\n");
				continue;
			}

			cfb_framebuffer_finalize(dev);
#if defined(CONFIG_ARCH_POSIX)
			k_sleep(K_MSEC(50));
#endif
		}
	}*/
	return 0;
}
