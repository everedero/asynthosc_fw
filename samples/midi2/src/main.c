/*
 * Copyright (c) 2025 Titouan Christophe
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <errno.h>
#include <stdio.h>

#include <zephyr/audio/midi.h>
#include <zephyr/net/dns_sd.h>
#include <zephyr/net/midi2.h>

#include <ump_stream_responder.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/audio/midi.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_midi2_sample, LOG_LEVEL_DBG);

#define ACT_LED_NODE DT_NODELABEL(midi_green_led)
#define SERIAL_NODE  DT_NODELABEL(midi_serial)

#define MSG_SIZE 32

#if !DT_NODE_EXISTS(ACT_LED_NODE)
#define CONFIGURE_LED()
#define SET_LED(_state)
#else /* DT_NODE_EXISTS(ACT_LED_NODE) */
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec act_led = GPIO_DT_SPEC_GET(ACT_LED_NODE, gpios);

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

#define CONFIGURE_LED() gpio_pin_configure_dt(&act_led, GPIO_OUTPUT_INACTIVE)
#define SET_LED(_state) gpio_pin_set_dt(&act_led, (_state))
#endif  /* DT_NODE_EXISTS(ACT_LED_NODE) */


#if !DT_NODE_EXISTS(SERIAL_NODE)
#define send_external_midi1(...)
#else /* DT_NODE_EXISTS(SERIAL_NODE) */
#include <zephyr/drivers/uart.h>

static const struct device *const uart_dev = DEVICE_DT_GET(SERIAL_NODE);

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static inline void send_external_midi1(const struct midi_ump ump)
{
	/* Only send MIDI events aimed at the external output */
	if (UMP_GROUP(ump) != DT_REG_ADDR(DT_NODELABEL(ext_midi_in))) {
		return;
	}

	switch (UMP_MIDI_COMMAND(ump)) {
	case UMP_MIDI_PROGRAM_CHANGE:
		SET_LED(1);
		uart_poll_out(uart_dev, UMP_MIDI_STATUS(ump));
		uart_poll_out(uart_dev, UMP_MIDI1_P1(ump));
		SET_LED(0);
		break;

	case UMP_MIDI_NOTE_OFF:
	case UMP_MIDI_NOTE_ON:
	case UMP_MIDI_AFTERTOUCH:
	case UMP_MIDI_CONTROL_CHANGE:
	case UMP_MIDI_PITCH_BEND:
		SET_LED(1);
		uart_poll_out(uart_dev, UMP_MIDI_STATUS(ump));
		uart_poll_out(uart_dev, UMP_MIDI1_P1(ump));
		uart_poll_out(uart_dev, UMP_MIDI1_P2(ump));
		SET_LED(0);
		break;
	}
}
#endif /* DT_NODE_EXISTS(SERIAL_NODE) */


static const struct ump_endpoint_dt_spec ump_ep_dt =
	UMP_ENDPOINT_DT_SPEC_GET(DT_NODELABEL(midi2));

static inline void handle_ump_stream(struct netmidi2_session *session,
				     const struct midi_ump ump)
{
	const struct ump_stream_responder_cfg responder_cfg =
		UMP_STREAM_RESPONDER(session, netmidi2_send, &ump_ep_dt);
	ump_stream_respond(&responder_cfg, ump);
}

static void netmidi2_callback(struct netmidi2_session *session,
			      const struct midi_ump ump)
{
	switch (UMP_MT(ump)) {
/*	case UMP_MT_MIDI1_CHANNEL_VOICE:
		send_external_midi1(ump);
		break;*/
	case UMP_MT_UMP_STREAM:
		handle_ump_stream(session, ump);
		break;
	}
}

#if defined(CONFIG_NET_SAMPLE_MIDI2_AUTH_NONE)
/* Simple Network MIDI 2.0 endpoint without authentication */
NETMIDI2_EP_DEFINE(midi_server, ump_ep_dt.name, NULL, 0);

#elif defined(CONFIG_NET_SAMPLE_MIDI2_AUTH_SHARED_SECRET)
/* Network MIDI 2.0 endpoint with shared secret authentication */
BUILD_ASSERT(
	sizeof(CONFIG_NET_SAMPLE_MIDI2_SHARED_SECRET) > 1,
	"CONFIG_NET_SAMPLE_MIDI2_SHARED_SECRET must be not empty"
);

NETMIDI2_EP_DEFINE_WITH_AUTH(midi_server, ump_ep_dt.name, NULL, 0,
	CONFIG_NET_SAMPLE_MIDI2_SHARED_SECRET);

#elif defined(CONFIG_NET_SAMPLE_MIDI2_AUTH_USER_PASSWORD)
/* Network MIDI 2.0 endpoint with a single user/password*/
BUILD_ASSERT(
	sizeof(CONFIG_NET_SAMPLE_MIDI2_USERNAME) > 1,
	"CONFIG_NET_SAMPLE_MIDI2_USERNAME must be not empty"
);
BUILD_ASSERT(
	sizeof(CONFIG_NET_SAMPLE_MIDI2_PASSWORD) > 1,
	"CONFIG_NET_SAMPLE_MIDI2_PASSWORD must be not empty"
);

NETMIDI2_EP_DEFINE_WITH_USERS(midi_server, ump_ep_dt.name, NULL, 0,
	{.name = CONFIG_NET_SAMPLE_MIDI2_USERNAME,
	 .password = CONFIG_NET_SAMPLE_MIDI2_PASSWORD});

#endif

DNS_SD_REGISTER_SERVICE(midi_dns, CONFIG_NET_HOSTNAME "-" CONFIG_BOARD,
			"_midi2", "_udp", "local", DNS_SD_EMPTY_TXT,
			&midi_server.addr4.sin_port);

void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;
	static int cnt = 0;
	static int len = 0;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
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

int main(void)
{
	CONFIGURE_LED();

	midi_server.rx_packet_cb = netmidi2_callback;
	netmidi2_host_ep_start(&midi_server);

	return 0;

//int main(void)
//{
	char tx_buf[MSG_SIZE];
	int i;
	struct midi_ump ump;

	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
		return 0;
	}


	/* configure interrupt and callback to receive data */
	int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);

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
	uart_irq_rx_enable(uart_dev);

	/* indefinitely wait for input from the user */
	while (k_msgq_get(&uart_msgq, &tx_buf, K_FOREVER) == 0) {
		LOG_DBG("MIDI cmd 0x%x", tx_buf[0]);
		if ((tx_buf[0] & 0xf0) == 0x80 || (tx_buf[0] & 0xf0) == 0x90) {
			LOG_HEXDUMP_INF(tx_buf, 3, "MIDI msg");
			ump = UMP_MIDI1_CHANNEL_VOICE((tx_buf[0] & 0x0f), (tx_buf[0] & 0xf0) >> 4,
					0, tx_buf[1], tx_buf[2]);
			// SEND MIDI
	netmidi2_send(struct netmidi2_session *sess, const struct midi_ump ump)
		}
	}
	return 0;
}
