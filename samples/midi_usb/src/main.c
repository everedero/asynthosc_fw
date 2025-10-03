/*
 * Copyright (c) 2024 Titouan Christophe
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file
 * @brief Sample application for USB MIDI 2.0 device class
 */

#include <sample_usbd.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/class/usbd_midi2.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/audio/midi.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample_usb_midi, LOG_LEVEL_INF);

#define MIDI_NODE DT_NODELABEL(midi)
#define MSG_SIZE 32

static const struct device *const usbmidi = DEVICE_DT_GET(DT_NODELABEL(usb_midi));

static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static const struct device *const uart_midi_dev = DEVICE_DT_GET(MIDI_NODE);

/* receive buffer used in UART ISR callback */
static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

#define UMP_NUM_WORDS_LOOKUP_TABLE \
	((0U << 0) | (0U << 2) | (0U << 4) | (1U << 6) | \
	(1U << 8) | (3U << 10) | (0U << 12) | (0U << 14) | \
	(2U << 16) | (2U << 18) | (1U << 20) | (2U << 22) | \
	(2U << 24) | (3U << 26) | (3U << 28) | (3U << 30))

#define UMP_NUM_WORDS(ump) \
	((UMP_NUM_WORDS_LOOKUP_TABLE >> (2 * ump)) & 0b11)

void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;
	static int cnt = 0;
	static int len = 0;

	if (!uart_irq_update(uart_midi_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_midi_dev)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_midi_dev, &c, 1) == 1) {
		//if ((c == 0xf8) && rx_buf_pos > 0) {
		if (cnt == 0) {
			if (c >= 0xf0) {
				len = 0;
			} else if (c < 0xf0) {
				len = UMP_NUM_WORDS((c & 0xf0) >> 4);
			}
			rx_buf[cnt] = c;
			cnt++;
		}
		else if (cnt > 0 && cnt <= len) {
			rx_buf[cnt] = c;
			cnt++;
		}

		if (cnt > len) {
			/* terminate string */
			rx_buf[cnt] = '\0';

			/* if queue is full, message is silently dropped */
			if (len > 0) {
			    /* Only keeping long messages */
			    k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);
			}

			/* reset the buffer (it was copied to the msgq) */
			cnt = 0;
			len = 0;
			rx_buf_pos = 0;
		}
	}
}

static void on_midi_packet(const struct device *dev, const struct midi_ump ump)
{
	LOG_INF("Received MIDI packet (MT=%X)", UMP_MT(ump));

	/* Only send MIDI1 channel voice messages back to the host */
	if (UMP_MT(ump) == UMP_MT_MIDI1_CHANNEL_VOICE) {
		LOG_INF("Send back MIDI1 message %02X %02X %02X", UMP_MIDI_STATUS(ump),
			UMP_MIDI1_P1(ump), UMP_MIDI1_P2(ump));
		usbd_midi_send(dev, ump);
	}
}

static void on_device_ready(const struct device *dev, const bool ready)
{
	/* Light up the LED (if any) when USB-MIDI2.0 is enabled */
	if (led.port) {
		gpio_pin_set_dt(&led, ready);
	}
}

static const struct usbd_midi_ops ops = {
	.rx_packet_cb = on_midi_packet,
	.ready_cb = on_device_ready,
};

int main(void)
{
	char tx_buf[MSG_SIZE];
	int i;
	struct usbd_context *sample_usbd;
	struct midi_ump ump;

	if (!device_is_ready(usbmidi)) {
		LOG_ERR("MIDI device not ready");
		return -1;
	}

	if (!device_is_ready(uart_midi_dev)) {
		printk("UART device not found!");
		return 0;
	}

	if (led.port) {
		if (gpio_pin_configure_dt(&led, GPIO_OUTPUT)) {
			LOG_ERR("Unable to setup LED, not using it");
			memset(&led, 0, sizeof(led));
		}
	}

	usbd_midi_set_ops(usbmidi, &ops);

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -1;
	}

	if (usbd_enable(sample_usbd)) {
		LOG_ERR("Failed to enable device support");
		return -1;
	}

	LOG_INF("USB device support enabled");

	/* configure interrupt and callback to receive data */
	int ret = uart_irq_callback_user_data_set(uart_midi_dev, serial_cb, NULL);

	if (ret < 0) {
		if (ret == -ENOTSUP) {
			printk("Interrupt-driven UART API support not enabled\n");
		} else if (ret == -ENOSYS) {
			printk("UART device does not support interrupt-driven API\n");
		} else {
			printk("Error setting UART callback: %d\n", ret);
		}
		return 0;
	}
	uart_irq_rx_enable(uart_midi_dev);

	/* indefinitely wait for input from the user */
	while (k_msgq_get(&uart_msgq, &tx_buf, K_FOREVER) == 0) {
		LOG_DBG("MIDI cmd 0x%x", tx_buf[0]);
		if ((tx_buf[0] & 0xf0) == 0x80 || (tx_buf[0] & 0xf0) == 0x90) {
			LOG_HEXDUMP_INF(tx_buf, 3, "MIDI msg");
			ump = UMP_MIDI1_CHANNEL_VOICE((tx_buf[0] & 0x0f), (tx_buf[0] & 0xf0) >> 4,
					0, tx_buf[1], tx_buf[2]);
			usbd_midi_send(usbmidi, ump);
		}
	}
	return 0;
}
