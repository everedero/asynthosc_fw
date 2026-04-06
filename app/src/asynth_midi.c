/*
 * Asynth MIDI input helpers.
 */

#include <app/asynth_midi.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <app/asynth_osc_bridge.h>
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
static bool midi_note_forward_enabled;

/* MIDI parser state machine */
static uint8_t midi_running_status;
static uint8_t midi_data_bytes[2];
static uint8_t midi_data_count;

/* SysEx buffer (MMC, MTC Full Frame) */
#define MIDI_SYSEX_BUF_LEN 32U
static uint8_t midi_sysex_buf[MIDI_SYSEX_BUF_LEN];
static uint8_t midi_sysex_len;
static bool midi_in_sysex;

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

static void midi_dispatch_sysex(const uint8_t *buf, uint8_t len)
{
	/* Require Universal Real-Time SysEx header: 7F <devID> ... */
	if (len < 4U || buf[0] != 0x7FU) {
		return;
	}

	/* MMC: F0 7F <devID> 06 <cmd> F7 → buf={7F devID 06 cmd}, len=4 */
	if (buf[2] == 0x06U) {
		(void)asynth_osc_send_midi_mmc(buf[1], buf[3]);
		return;
	}

	/* MTC Full Frame: F0 7F <devID> 01 01 <hr> <mn> <se> <fr> F7
	 * → buf={7F devID 01 01 hr mn se fr}, len=8
	 * packed = (hr << 24) | (mn << 16) | (se << 8) | fr
	 * hr byte encodes frame rate (bits 6-5) and hours (bits 4-0). */
	if (len >= 8U && buf[2] == 0x01U && buf[3] == 0x01U) {
		uint32_t packed = ((uint32_t)buf[4] << 24) |
				  ((uint32_t)buf[5] << 16) |
				  ((uint32_t)buf[6] <<  8) |
				   (uint32_t)buf[7];
		(void)asynth_osc_send_midi_mtc_ff(packed);
		return;
	}
}

static void midi_parse_and_dispatch(uint8_t byte)
{
	struct asynth_midi_msg msg;
	uint8_t status_nibble;

	/* System Real-Time (0xF8-0xFF): single byte, ignore */
	if (byte >= 0xF8U) {
		return;
	}

	/* SysEx in progress: collect bytes until 0xF7 end marker */
	if (midi_in_sysex) {
		if (byte == 0xF7U) {
			midi_in_sysex = false;
			midi_dispatch_sysex(midi_sysex_buf, midi_sysex_len);
		} else if (!(byte & 0x80U)) {
			if (midi_sysex_len < MIDI_SYSEX_BUF_LEN) {
				midi_sysex_buf[midi_sysex_len++] = byte;
			}
			/* else: buffer full, drop byte silently */
		} else {
			/* Unexpected status byte: abort SysEx, treat as new status */
			midi_in_sysex = false;
			midi_sysex_len = 0;
			midi_running_status = byte;
			midi_data_count = 0;
		}
		return;
	}

	if (byte & 0x80U) {
		/* Status byte */
		if (byte == 0xF0U) {
			/* SysEx start */
			midi_in_sysex = true;
			midi_sysex_len = 0;
			midi_running_status = 0;
			midi_data_count = 0;
			return;
		}

		/* Channel or System Common message */
		midi_running_status = byte;
		midi_data_count = 0;
		return;
	}

	/* Data byte */
	if (midi_running_status == 0) {
		return;
	}

	midi_data_bytes[midi_data_count++] = byte;

	status_nibble = midi_running_status & 0xF0U;
	msg.status = midi_running_status;
	msg.channel = midi_running_status & 0x0FU;
	msg.msg_type = (enum asynth_midi_msg_type)status_nibble;

	/* MTC Quarter Frame: 0xF1, 1 data byte (bits 6-4 = piece, bits 3-0 = value nibble) */
	if (midi_running_status == 0xF1U) {
		if (midi_data_count >= 1) {
			(void)asynth_osc_send_midi_mtc_qf(
				(midi_data_bytes[0] >> 4) & 0x07U,
				 midi_data_bytes[0] & 0x0FU);
			midi_data_count = 0;
		}
		return;
	}

	/* Channel messages */
	switch (status_nibble) {
	case 0xC0: /* Program Change - 1 data byte */
	case 0xD0: /* Channel Pressure - 1 data byte */
		if (midi_data_count >= 1) {
			msg.data1 = midi_data_bytes[0];
			msg.data2 = 0;

			if (midi_pc_handler && status_nibble == 0xC0) {
				midi_pc_handler(&msg);
			}

			if (status_nibble == 0xC0) {
				(void)asynth_osc_send_midi_pc(msg.channel, msg.data1);
			}

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
				if (midi_note_forward_enabled) {
					(void)asynth_osc_send_midi_note_on(msg.channel, msg.data1, msg.data2);
				}
			} else if (status_nibble == 0x80) {
				/* Note Off */
				if (midi_note_handler) {
					midi_note_handler(&msg);
				}
				if (midi_note_forward_enabled) {
					(void)asynth_osc_send_midi_note_off(msg.channel, msg.data1);
				}
			} else if (status_nibble == 0xB0) {
				/* Control Change */
				if (midi_cc_handler) {
					midi_cc_handler(&msg);
				}
				(void)asynth_osc_send_midi_cc(msg.channel, msg.data1, msg.data2);
			} else if (status_nibble == 0xE0) {
				/* Pitch Bend: LSB in data1, MSB in data2, 14-bit centered at 8192 */
				int16_t bend = (int16_t)(
					(int32_t)(((uint16_t)msg.data2 << 7) |
					           (uint16_t)msg.data1) - 8192);
				(void)asynth_osc_send_midi_pitch_bend(msg.channel, bend);
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

void asynth_midi_set_note_forward_enabled(bool enabled)
{
	midi_note_forward_enabled = enabled;
}

bool asynth_midi_is_note_forward_enabled(void)
{
	return midi_note_forward_enabled;
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
