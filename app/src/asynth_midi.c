/*
 * Asynth MIDI input helpers.
 */

#include <app/asynth_midi.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <app/asynth_display.h>

#define MIDI_IN_NODE DT_NODELABEL(midi)
#define MIDI_IN_MSGQ_LEN 128U
#define MIDI_IN_DIAG_LOG_PERIOD_MS 30000U

#if !DT_NODE_HAS_STATUS(MIDI_IN_NODE, okay)
#error "Missing or disabled MIDI UART node 'midi' in board DTS"
#endif

static const struct device *const midi_in_uart_dev = DEVICE_DT_GET(MIDI_IN_NODE);

K_MSGQ_DEFINE(midi_in_msgq, sizeof(uint8_t), MIDI_IN_MSGQ_LEN, 1);

static volatile uint32_t midi_in_drop_count;
static volatile uint32_t midi_in_isr_rx_count;
static volatile uint32_t midi_in_poll_rx_count;
static volatile uint32_t midi_in_uart_err_count;
static volatile int midi_in_last_uart_err;

/* MIDI message handlers */
static asynth_midi_handler_t midi_note_handler;
static asynth_midi_handler_t midi_cc_handler;
static asynth_midi_handler_t midi_pc_handler;

/* MIDI parser state machine */
static uint8_t midi_running_status;
static uint8_t midi_data_bytes[2];
static uint8_t midi_data_count;

static void midi_in_uart_cb(const struct device *dev, void *user_data)
{
	uint8_t byte;

	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev)) {
		return;
	}

	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	while (uart_fifo_read(dev, &byte, 1) == 1) {
		midi_in_isr_rx_count++;
		if (k_msgq_put(&midi_in_msgq, &byte, K_NO_WAIT) != 0) {
			midi_in_drop_count++;
		}
	}
}

int asynth_midi_init(void)
{
	int ret;
	struct uart_config cfg;

	if (!device_is_ready(midi_in_uart_dev)) {
		printk("MIDI IN: UART device not ready\n");
		return -ENODEV;
	}

	ret = uart_irq_callback_user_data_set(midi_in_uart_dev, midi_in_uart_cb, NULL);
	if (ret < 0) {
		if (ret == -ENOTSUP) {
			printk("MIDI IN: interrupt-driven UART API not enabled\n");
		} else if (ret == -ENOSYS) {
			printk("MIDI IN: UART device has no interrupt-driven support\n");
		} else {
			printk("MIDI IN: failed to set UART callback (%d)\n", ret);
		}

		return ret;
	}

	uart_irq_rx_enable(midi_in_uart_dev);

	ret = uart_config_get(midi_in_uart_dev, &cfg);
	if (ret == 0) {
		printk("MIDI IN: listening on %s @ %u baud\n",
		       midi_in_uart_dev->name,
		       (unsigned int)cfg.baudrate);
	} else {
		printk("MIDI IN: listening on %s\n", midi_in_uart_dev->name);
	}

	midi_running_status = 0;
	midi_data_count = 0;

	return 0;
}

void asynth_midi_poll_fallback(void)
{
	uint8_t byte;

	while (uart_poll_in(midi_in_uart_dev, &byte) == 0) {
		midi_in_poll_rx_count++;
		if (k_msgq_put(&midi_in_msgq, &byte, K_NO_WAIT) != 0) {
			midi_in_drop_count++;
		}
	}
}

void asynth_midi_sample_uart_errors(void)
{
	int err = uart_err_check(midi_in_uart_dev);

	if (err != 0) {
		midi_in_uart_err_count++;
		midi_in_last_uart_err = err;
	}
}

void asynth_midi_log_diag(uint32_t *last_log_ms)
{
	static uint32_t prev_isr_count;
	static uint32_t prev_poll_count;
	static uint32_t prev_drop_count;
	static uint32_t prev_err_count;
	static int prev_last_err;
	uint32_t now_ms = k_uptime_get_32();
	uint32_t isr_count = midi_in_isr_rx_count;
	uint32_t poll_count = midi_in_poll_rx_count;
	uint32_t drop_count = midi_in_drop_count;
	uint32_t err_count = midi_in_uart_err_count;
	int last_err = midi_in_last_uart_err;
	bool changed;

	changed = (isr_count != prev_isr_count) ||
			  (poll_count != prev_poll_count) ||
			  (drop_count != prev_drop_count) ||
			  (err_count != prev_err_count) ||
			  (last_err != prev_last_err);

	if (!changed) {
		return;
	}

	if ((uint32_t)(now_ms - *last_log_ms) < MIDI_IN_DIAG_LOG_PERIOD_MS) {
		return;
	}

	*last_log_ms = now_ms;
	prev_isr_count = isr_count;
	prev_poll_count = poll_count;
	prev_drop_count = drop_count;
	prev_err_count = err_count;
	prev_last_err = last_err;

	printk("MIDI DIAG: isr=%u poll=%u drop=%u err=%u last_err=0x%x\n",
	       (unsigned int)isr_count,
	       (unsigned int)poll_count,
	       (unsigned int)drop_count,
	       (unsigned int)err_count,
	       (unsigned int)last_err);
}

static void midi_parse_and_dispatch(uint8_t byte)
{
	struct asynth_midi_msg msg;
	uint8_t status_nibble;

	/* Handle system messages and status bytes */
	if (byte & 0x80) {
		/* This is a status byte */
		if (byte == 0xF0 || byte == 0xF7) {
			/* Sysex - not supported, reset parser */
			midi_running_status = 0;
			midi_data_count = 0;
			return;
		} else if (byte >= 0xF8) {
			/* System Real-Time - single byte, ignored for now */
			return;
		}

		/* Channel message status byte */
		midi_running_status = byte;
		midi_data_count = 0;
		return;
	}

	/* This is a data byte */
	if (midi_running_status == 0) {
		/* No running status, ignore */
		return;
	}

	midi_data_bytes[midi_data_count++] = byte;

	status_nibble = midi_running_status & 0xF0;
	msg.status = midi_running_status;
	msg.channel = midi_running_status & 0x0F;
	msg.msg_type = (enum asynth_midi_msg_type)status_nibble;

	/* Determine expected data byte count and dispatch when complete */
	switch (status_nibble) {
	case 0xC0: /* Program Change - 1 data byte */
	case 0xD0: /* Channel Pressure - 1 data byte */
		if (midi_data_count >= 1) {
			msg.data1 = midi_data_bytes[0];
			msg.data2 = 0;

			if (midi_pc_handler && status_nibble == 0xC0) {
				midi_pc_handler(&msg);
			}

			printk("MIDI: %s CH%u VAL=%u\n",
			       status_nibble == 0xC0 ? "PC" : "CP",
			       (unsigned int)msg.channel + 1,
			       (unsigned int)msg.data1);

			midi_data_count = 0;
		}
		break;

	case 0x80: /* Note Off - 2 data bytes */
	case 0x90: /* Note On - 2 data bytes */
	case 0xA0: /* Polyphonic Key Pressure - 2 data bytes */
	case 0xB0: /* Control Change - 2 data bytes */
	case 0xE0: /* Pitch Bend - 2 data bytes */
		if (midi_data_count >= 2) {
			msg.data1 = midi_data_bytes[0];
			msg.data2 = midi_data_bytes[1];

			if (status_nibble == 0x90) {
				/* Note On */
				if (midi_note_handler) {
					midi_note_handler(&msg);
				}
				printk("MIDI: Note On CH%u NOTE=%u VEL=%u\n",
				       (unsigned int)msg.channel + 1,
				       (unsigned int)msg.data1,
				       (unsigned int)msg.data2);
			} else if (status_nibble == 0x80) {
				/* Note Off */
				if (midi_note_handler) {
					midi_note_handler(&msg);
				}
				printk("MIDI: Note Off CH%u NOTE=%u VEL=%u\n",
				       (unsigned int)msg.channel + 1,
				       (unsigned int)msg.data1,
				       (unsigned int)msg.data2);
			} else if (status_nibble == 0xB0) {
				/* Control Change */
				if (midi_cc_handler) {
					midi_cc_handler(&msg);
				}
				printk("MIDI: CC CH%u CC=%u VAL=%u\n",
				       (unsigned int)msg.channel + 1,
				       (unsigned int)msg.data1,
				       (unsigned int)msg.data2);
			}

			midi_data_count = 0;
		}
		break;

	default:
		break;
	}
}

bool asynth_midi_process_events(void)
{
	uint8_t byte;
	bool ui_dirty = false;

	while (k_msgq_get(&midi_in_msgq, &byte, K_NO_WAIT) == 0) {
		midi_parse_and_dispatch(byte);
		asynth_display_act_m();
		ui_dirty = true;
	}

	return ui_dirty;
}

uint32_t asynth_midi_take_drop_count(void)
{
	uint32_t dropped = midi_in_drop_count;

	if (dropped != 0U) {
		midi_in_drop_count = 0U;
	}

	return dropped;
}

/* Handler registration */
void asynth_midi_set_note_handler(asynth_midi_handler_t handler)
{
	midi_note_handler = handler;
}

void asynth_midi_set_control_change_handler(asynth_midi_handler_t handler)
{
	midi_cc_handler = handler;
}

void asynth_midi_set_program_change_handler(asynth_midi_handler_t handler)
{
	midi_pc_handler = handler;
}
