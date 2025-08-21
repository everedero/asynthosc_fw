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

static char tx_buffer[256]; // declare a 256b buffer to read packet data into
int max_len = 256;

ZTEST(tinyosc_lib, test_bundle_id)
{
 /*defaults to "#bundle" 0x2362756E646C6500*/
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

ZTEST(tinyosc_lib, test_bundle_timestamp)
{
  tosc_bundle bundle;
  uint32_t len;
  uint64_t timestamp;
  // This is the desired output
  uint8_t testBuffer[] = {35, 98, 117, 110, 100, 108, 101, 0, 0xfe, 0xed, 0xde, 0xad, 0xc0, 0x07, 0xca, 0xfe};
  // Writing timestamp in bundle
  tosc_writeBundle(&bundle, 0xfeeddeadc007cafe, tx_buffer, max_len);
  len = tosc_getBundleLength(&bundle);
  zassert_equal(len, sizeof(testBuffer), "Length mismatch");
  // Retrieving timestamp from bundle
  timestamp = tosc_getTimetag(&bundle);
  zassert_equal(timestamp, 0xfeeddeadc007cafe, "Write and read timestamp failed");
  // Testing all buffer
  for (int i = 0; i < sizeof(testBuffer); i++){
    zassert_equal(testBuffer[i], tx_buffer[i], "Value mismatch at index %d. Expected 0x%02x, got 0x%02x", i, testBuffer[i], tx_buffer[i]);
  }
}

/* Test vectors copied from OSCBundle_test.ino from https://github.com/CNMAT/OSC */
/* Will only work with timestamps to 0 and default #bundle separator */
ZTEST(tinyosc_lib, test_bundle_encode)
{
  tosc_bundle bundle;
  uint32_t len;
  // This is the desired output
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

ZTEST(tinyosc_lib, test_bundle_decode)
{
  tosc_bundle bundle;
  bool is_msg;
  uint8_t testBuffer[] = {35, 98, 117, 110, 100, 108, 101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 47, 97, 0, 0, 44, 105, 0, 0, 0, 0, 0, 1, 0, 0, 0, 12, 47, 98, 0, 0, 44, 105, 0, 0, 0, 0, 0, 2};
  tosc_message msg;
  // All timestamps to zero for test
  tosc_parseBundle(&bundle, testBuffer, sizeof(testBuffer));
  zassert_equal(tosc_getTimetag(&bundle), 0, "Time tag should be 0");
  // First bundle message
  is_msg = tosc_getNextMessage(&bundle, &msg);
  zassert_true(is_msg, "Message not found");
  zassert_str_equal(tosc_getFormat(&msg), "i", "Wrong format, should be int");
  zassert_str_equal(tosc_getAddress(&msg), "/a", "Wrong address, should be /a");
  zassert_equal(tosc_getNextInt32(&msg), 1, "Wrong integer, should be 1");

  // Second bundle message
  is_msg = tosc_getNextMessage(&bundle, &msg);
  zassert_true(is_msg, "Message not found");
  zassert_str_equal(tosc_getFormat(&msg), "i", "Wrong format (2), should be int");
  zassert_str_equal(tosc_getAddress(&msg), "/b", "Wrong address, should be /b");
  zassert_equal(tosc_getNextInt32(&msg), 2, "Wrong integer, should be 2");

  is_msg = tosc_getNextMessage(&bundle, &msg);
  zassert_false(is_msg, "Message detected at end of bundle");
}

ZTEST_SUITE(tinyosc_lib, NULL, NULL, NULL, NULL, NULL);
