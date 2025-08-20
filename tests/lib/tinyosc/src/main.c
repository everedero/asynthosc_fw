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

static char tx_buffer[2048]; // declare a 2Kb buffer to read packet data into
int max_len = 2048;

ZTEST(tinyosc_lib, test_bundle_id)
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


/* Test vectors copied from OSCBundle_test.ino from https://github.com/CNMAT/OSC*/
ZTEST(tinyosc_lib, test_bundle_create)
{
  // TestPrint printer;
  tosc_bundle bundle;
  uint32_t len;
  //this is the desired output
  uint8_t testBuffer[] = {35, 98, 117, 110, 100, 108, 101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 47, 97, 0, 0, 44, 105, 0, 0, 0, 0, 0, 1, 0, 0, 0, 12, 47, 98, 0, 0, 44, 105, 0, 0, 0, 0, 0, 2};
  // All timestamps to zero for test
  tosc_writeBundle(&bundle, 0, tx_buffer, max_len);
  tosc_writeNextMessage(&bundle, "/a", "i", 1);
  tosc_writeNextMessage(&bundle, "/b", "i", 2);
  len = tosc_getBundleLength(&bundle);
  zassert_equal(len, sizeof(testBuffer), "Length mismatch");
  for (int i = 0; i < sizeof(testBuffer); i++){
    zassert_equal(testBuffer[i], tx_buffer[i], "Value mismatch at index %d. Expected 0x%02x, got 0x%02x", i, testBuffer[i], tx_buffer[i]);
  }
}


ZTEST_SUITE(tinyosc_lib, NULL, NULL, NULL, NULL, NULL);
