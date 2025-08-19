/*
 * Copyright (c) 2021 Legrand North America, LLC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * @file test tinyosc_lib library
 *
 * This suite verifies that the methods provided with the tinyosc
 * library works correctly.
 */

#include <limits.h>

#include <zephyr/ztest.h>

#include <app/lib/tinyosc.h>

ZTEST(tinyosc_lib, test_get_value)
{
 /*default 2549729456036799744 #0x2362756E646C6500*/
	uint8_t buffer[8] = {0x23, 0x62, 0x75, 0x6E, 0x64, 0x6C, 0x65, 0x00};
	bool tested;
	tested = tosc_isBundle(buffer);
	if (CONFIG_TINYOSC_BUNDLE_ID == 2549729456036799744) {
		zassert_true(tested, "bundle id ok");
	}
	else {
		zassert_false(tested, "modified bundle id found");
	}
}

ZTEST_SUITE(tinyosc_lib, NULL, NULL, NULL, NULL, NULL);
