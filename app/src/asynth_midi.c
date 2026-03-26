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
#define MIDI_IN_DIAG_LOG_PERIOD_MS 1000U

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
	uint32_t now_ms = k_uptime_get_32();

	if ((uint32_t)(now_ms - *last_log_ms) < MIDI_IN_DIAG_LOG_PERIOD_MS) {
		return;
	}

	*last_log_ms = now_ms;
	printk("MIDI DIAG: isr=%u poll=%u drop=%u err=%u last_err=0x%x\n",
	       (unsigned int)midi_in_isr_rx_count,
	       (unsigned int)midi_in_poll_rx_count,
	       (unsigned int)midi_in_drop_count,
	       (unsigned int)midi_in_uart_err_count,
	       (unsigned int)midi_in_last_uart_err);
}

bool asynth_midi_process_events(void)
{
	uint8_t byte;
	bool ui_dirty = false;

	while (k_msgq_get(&midi_in_msgq, &byte, K_NO_WAIT) == 0) {
		printk("MIDI IN: 0x%02x\n", byte);
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
