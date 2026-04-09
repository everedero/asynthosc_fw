/*
 * Asynth2OSC module main code
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <app/asynth_cv.h>
#include <app/asynth_display.h>
#include <app/asynth_midi.h>
#include <app/asynth_osc_bridge.h>
#include <app/asynth_ui.h>
#if defined(CONFIG_NETWORKING)
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <app/lib/tinyosc.h>
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifndef ASYNTH_BUILD_ID
#define ASYNTH_BUILD_ID "b:unknown"
#endif

const struct gpio_dt_spec right_led = GPIO_DT_SPEC_GET(DT_NODELABEL(right_button_led), gpios);
const struct gpio_dt_spec left_led = GPIO_DT_SPEC_GET(DT_NODELABEL(left_button_led), gpios);
const struct gpio_dt_spec right_button = GPIO_DT_SPEC_GET(DT_NODELABEL(right_button), gpios);
const struct gpio_dt_spec left_button = GPIO_DT_SPEC_GET(DT_NODELABEL(left_button), gpios);
const struct gpio_dt_spec rot_button = GPIO_DT_SPEC_GET(DT_NODELABEL(rot_button), gpios);
const struct gpio_dt_spec rot_encoder_a = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(gpioqdec), gpios, 0);
const struct gpio_dt_spec rot_encoder_b = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(gpioqdec), gpios, 1);
const struct gpio_dt_spec trigger_1 = GPIO_DT_SPEC_GET(DT_NODELABEL(trigger_1), gpios);
const struct gpio_dt_spec trigger_2 = GPIO_DT_SPEC_GET(DT_NODELABEL(trigger_2), gpios);
const struct device *oled;

#define CV_CHANNEL_COUNT      4
#define AOUT_DAC_CHANNEL_ID   1U
#define AOUT_DAC_RESOLUTION   12U
#define AOUT_DAC_MAX_VALUE    ((1U << AOUT_DAC_RESOLUTION) - 1U)
#define CV_BAR_MAX_PIXELS     28U
#define CV_ADC_RESOLUTION     12U
#define CV_ADC_MAX_RAW        ((1U << CV_ADC_RESOLUTION) - 1U)
#define CV_ADC_FULL_SCALE_MV  3300U

#define CV_SAMPLE_PERIOD_MIN_MS           10U
#define CV_SAMPLE_PERIOD_MAX_MS           200U
#define CV_SAMPLE_PERIOD_STEP_MS          5U
#define CV_SAMPLE_PERIOD_DEFAULT_MS       20U

#define CV_HYSTERESIS_MIN_PERMILLE        5U
#define CV_HYSTERESIS_MAX_PERMILLE        50U
#define CV_HYSTERESIS_STEP_PERMILLE       5U
#define CV_HYSTERESIS_DEFAULT_PERMILLE    10U

#define BUTTON_DEBOUNCE_MS    30U
#define ROTARY_LONG_PRESS_MS  2000U
#define ROTARY_DETENT_STEPS   4
#define CUE_PENDING_TIMEOUT_MS 5000U
#define CUE_PENDING_BLINK_INTERVAL_MS 300U
#define SPLASH_SCREEN_DURATION_MS 500U
#define IDLE_LED_TOGGLE_INTERVAL_MS 125U
#define NET_LINK_POLL_INTERVAL_MS 250U
#define NET_ACTIVITY_BLINK_MIN_INTERVAL_MS 80U

#define TRIGGER_EVENT_LOG_ENABLE         0U
#define BUTTON_EVENT_LOG_ENABLE          0U

#if TRIGGER_EVENT_LOG_ENABLE
#define TRIGGER_EVENT_LOG(...) printk(__VA_ARGS__)
#else
#define TRIGGER_EVENT_LOG(...) do { } while (0)
#endif

#if BUTTON_EVENT_LOG_ENABLE
#define BUTTON_EVENT_LOG(...) printk(__VA_ARGS__)
#else
#define BUTTON_EVENT_LOG(...) do { } while (0)
#endif

#define UI_LEFT_X             0U
#define UI_CENTER_X           43U
#define UI_RIGHT_X            86U
#define UI_BUTTON_LABEL_Y     49U
#define UI_BUTTON_INVERT_Y    51U
#define UI_BUTTON_WIDTH       42U
#define UI_BUTTON_HEIGHT      12U
#define UI_MODE_LABEL_NORMAL  "Recl"
#define UI_MODE_LABEL_MENU    "Menu"

#define NET_SETTINGS_PORT_BASE               42000U
#define NET_SETTINGS_PORT_OFFSET_MAX         999U
#define NET_SETTINGS_IP_BYTE_MAX             255U
#define NET_SETTINGS_CIDR_MAX                32U
#define NET_SETTINGS_IP_MODE_MAX             2U
#define MIDI_SETTINGS_NOTE_THRU_MAX          1U

#define NET_SETTINGS_DEFAULT_IP_MODE         0U
#define NET_SETTINGS_DEFAULT_TARGET_IP4      142U
#define NET_SETTINGS_DEFAULT_TARGET_PORT     10U
#define NET_SETTINGS_DEFAULT_DEVICE_IP4      42U
#define NET_SETTINGS_DEFAULT_DEVICE_PORT     11U
#define NET_SETTINGS_DEFAULT_CIDR_MASK       24U
#define NET_SETTINGS_DEFAULT_IP_B1           192U
#define NET_SETTINGS_DEFAULT_IP_B2           168U
#define NET_SETTINGS_DEFAULT_IP_B3           1U

#define NET_SETTINGS_KEY_IP_MODE             "asynth/net/ip_mode"
#define NET_SETTINGS_KEY_TARGET_IP4          "asynth/net/target_ip4"
#define NET_SETTINGS_KEY_TARGET_PORT         "asynth/net/target_port"
#define NET_SETTINGS_KEY_DEVICE_IP4          "asynth/net/device_ip4"
#define NET_SETTINGS_KEY_DEVICE_PORT         "asynth/net/device_port"
#define NET_SETTINGS_KEY_CIDR_MASK           "asynth/net/cidr_mask"
#define NET_SETTINGS_KEY_IP_B1               "asynth/net/ip_b1"
#define NET_SETTINGS_KEY_IP_B2               "asynth/net/ip_b2"
#define NET_SETTINGS_KEY_IP_B3               "asynth/net/ip_b3"

#define ADC_SETTINGS_KEY_SAMPLE_PERIOD_MS    "asynth/adc/sample_period_ms"
#define ADC_SETTINGS_KEY_HYSTERESIS_PERMILLE "asynth/adc/hysteresis_permille"
#define MIDI_SETTINGS_KEY_NOTE_THRU          "asynth/midi/note_thru"

#define CUE_SETTINGS_KEY_CURRENT_VALUE       "asynth/cue/current_value"

/* OSC message paths */
#define OSC_PATH_CV1                "/cv/1"
#define OSC_PATH_CV2                "/cv/2"
#define OSC_PATH_CV3                "/cv/3"
#define OSC_PATH_CV4                "/cv/4"
#define OSC_PATH_TRIGGER1           "/trig/1"
#define OSC_PATH_TRIGGER2           "/trig/2"
#define OSC_PATH_MIDI_NOTE      "/note"
#define OSC_PATH_MIDI_NOTE_OFF   "/note_off"
#define OSC_PATH_MIDI_PC         "/program"
#define OSC_PATH_MIDI_CC         "/control"
#define OSC_PATH_MIDI_PITCH_BEND "/pitch"
#define OSC_PATH_MIDI_CLOCK      "/clock"
#define OSC_PATH_MIDI_START      "/start"
#define OSC_PATH_MIDI_STOP       "/stop"
#define OSC_PATH_MIDI_CONTINUE   "/continue"
#define OSC_PATH_MIDI_SONGPOS    "/songpos"
#define OSC_PATH_MIDI_MTC_QF     "/mtc_qf"
#define OSC_PATH_MIDI_MTC_FF     "/mtc"
#define OSC_PATH_PING               "/ping"
#define OSC_PATH_PONG               "/pong"
#define OSC_PATH_MSG                "/msg"
#define OSC_PATH_CUE                "/cue"
#define OSC_PATH_IDLE               "/idle"
#define OSC_PATH_AOUT               "/aout"

enum app_mode {
	APP_MODE_NORMAL = 0,
	APP_MODE_MENU,
};

enum menu_item {
	MENU_ITEM_SAMPLE_PERIOD = 0,
	MENU_ITEM_HYSTERESIS,
	MENU_ITEM_NET_IP_MODE,
	MENU_ITEM_NET_TARGET_IP4,
	MENU_ITEM_NET_TARGET_PORT,
	MENU_ITEM_NET_DEVICE_IP4,
	MENU_ITEM_NET_DEVICE_PORT,
	MENU_ITEM_NET_CIDR_MASK,
	MENU_ITEM_NET_IP_B1,
	MENU_ITEM_NET_IP_B2,
	MENU_ITEM_NET_IP_B3,
	MENU_ITEM_MIDI_NOTE_THRU,
	MENU_ITEM_COUNT,
};

struct net_menu_settings {
	uint16_t ip_mode;
	uint16_t target_ip4;
	uint16_t target_port;
	uint16_t device_ip4;
	uint16_t device_port;
	uint16_t cidr_mask;
	uint16_t ip_b1;
	uint16_t ip_b2;
	uint16_t ip_b3;
};

static int asynth_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				      void *cb_arg);

static struct settings_handler asynth_settings_handler = {
	.name = "asynth",
	.h_set = asynth_settings_set,
};

/* Global CV settings (to be replaced by menu/settings later). */
static uint32_t cv_sample_period_ms = CV_SAMPLE_PERIOD_DEFAULT_MS;
static uint16_t cv_hysteresis_permille = CV_HYSTERESIS_DEFAULT_PERMILLE;
static float cv_hysteresis_norm = 0.01f;
static uint16_t current_cue_value = 0U;
static bool cue_pending_active;
static bool cue_pending_visible = true;
static uint16_t cue_pending_value;
static uint32_t cue_pending_last_change_ms;
static uint32_t cue_pending_last_blink_ms;
static struct net_menu_settings net_cfg = {
	.ip_mode = NET_SETTINGS_DEFAULT_IP_MODE,
	.target_ip4 = NET_SETTINGS_DEFAULT_TARGET_IP4,
	.target_port = NET_SETTINGS_DEFAULT_TARGET_PORT,
	.device_ip4 = NET_SETTINGS_DEFAULT_DEVICE_IP4,
	.device_port = NET_SETTINGS_DEFAULT_DEVICE_PORT,
	.cidr_mask = NET_SETTINGS_DEFAULT_CIDR_MASK,
	.ip_b1 = NET_SETTINGS_DEFAULT_IP_B1,
	.ip_b2 = NET_SETTINGS_DEFAULT_IP_B2,
	.ip_b3 = NET_SETTINGS_DEFAULT_IP_B3,
};
static bool net_settings_handler_registered;
static bool net_settings_ready;

static bool trigger_1_prev_state;
static bool trigger_2_prev_state;
static uint16_t midi_note_thru = 0U;
static bool idle_next_active;
static bool idle_next_led_on;
static uint32_t idle_next_led_toggle_ms;
static const struct device *const aout_dac_dev = DEVICE_DT_GET(DT_NODELABEL(dac1));
static bool aout_ready;
static uint16_t aout_last_raw_value;
static bool aout_warned_not_ready;

static enum app_mode current_mode = APP_MODE_NORMAL;
static uint8_t current_menu_item = MENU_ITEM_SAMPLE_PERIOD;
static uint32_t rot_press_start_ms;
static bool rot_press_tracking;
static bool rot_button_was_pressed_last_poll;
static int8_t rot_encoder_step_accum;

static void cue_pending_cancel(bool show_status);

static void app_idle_set(bool enabled)
{
	idle_next_active = enabled;
}

static void app_idle_clear_on_press(void)
{
	idle_next_active = false;
}

static bool app_osc_parse_optional_bool(const char *format, tosc_message *msg, bool *value)
{
	if (value == NULL) {
		return false;
	}

	if ((format == NULL) || (format[0] == '\0')) {
		*value = true;
		return true;
	}

	if (format[1] != '\0') {
		return false;
	}

	if (format[0] == 'T') {
		*value = true;
		return true;
	}

	if (format[0] == 'F') {
		*value = false;
		return true;
	}

	if (format[0] == 'i') {
		*value = (tosc_getNextInt32(msg) != 0);
		return true;
	}

	return false;
}

static int app_aout_init(void)
{
	struct dac_channel_cfg dac_cfg = {0};
	int ret;

	dac_cfg.channel_id = AOUT_DAC_CHANNEL_ID;
	dac_cfg.resolution = AOUT_DAC_RESOLUTION;
	dac_cfg.buffered = true;
	dac_cfg.internal = false;

	aout_ready = false;
	aout_last_raw_value = 0U;
	aout_warned_not_ready = false;

	if (!device_is_ready(aout_dac_dev)) {
		printk("AOUT: dac1 device not ready\n");
		return -ENODEV;
	}

	ret = dac_channel_setup(aout_dac_dev, &dac_cfg);
	if (ret < 0) {
		printk("AOUT: dac_channel_setup failed (%d)\n", ret);
		return ret;
	}

	ret = dac_write_value(aout_dac_dev, AOUT_DAC_CHANNEL_ID, 0U);
	if (ret < 0) {
		printk("AOUT: initial dac_write_value failed (%d)\n", ret);
		return ret;
	}

	aout_ready = true;
	return 0;
}

static int app_aout_set_normalized(float normalized)
{
	uint16_t raw;
	int ret = 0;

	if (normalized < 0.0f) {
		normalized = 0.0f;
	} else if (normalized > 1.0f) {
		normalized = 1.0f;
	}

	raw = (uint16_t)(normalized * (float)AOUT_DAC_MAX_VALUE + 0.5f);

	if (!aout_ready) {
		if (!aout_warned_not_ready) {
			printk("AOUT: command received but DAC not ready\n");
			aout_warned_not_ready = true;
		}
		asynth_display_set_a(raw > 0U);
		return -ENODEV;
	}

	if (raw != aout_last_raw_value) {
		ret = dac_write_value(aout_dac_dev, AOUT_DAC_CHANNEL_ID, raw);
		if (ret < 0) {
			printk("AOUT: dac_write_value raw=%u failed (%d)\n",
			       (unsigned int)raw, ret);
			return ret;
		}
		printk("AOUT: norm=%d.%03u raw=%u\n",
		       (int)normalized,
		       (unsigned int)((normalized - (float)((int)normalized)) * 1000.0f),
		       (unsigned int)raw);
		aout_last_raw_value = raw;
	}

	asynth_display_set_a(raw > 0U);
	return 0;
}

static bool app_idle_tick(uint32_t now_ms)
{
	/* Fast path: keep main loop overhead negligible when idle feature is inactive. */
	if (!idle_next_active) {
		if (idle_next_led_on) {
			idle_next_led_on = false;
			(void)gpio_pin_set_dt(&right_led, 0);
		}
		idle_next_led_toggle_ms = now_ms;
		return false;
	}

	if ((uint32_t)(now_ms - idle_next_led_toggle_ms) >= IDLE_LED_TOGGLE_INTERVAL_MS) {
		idle_next_led_toggle_ms = now_ms;
		idle_next_led_on = !idle_next_led_on;
		(void)gpio_pin_set_dt(&right_led, idle_next_led_on ? 1 : 0);
	}

	return false;
}

volatile uint32_t app_boot_stage;
volatile int32_t app_boot_error_code;

#define APP_BOOT_STAGE_ENTER_MAIN            0x01U
#define APP_BOOT_STAGE_GPIO_READY            0x02U
#define APP_BOOT_STAGE_UI_READY              0x03U
#define APP_BOOT_STAGE_TRIGGERS_READY        0x04U
#define APP_BOOT_STAGE_SETTINGS_READY        0x05U
#define APP_BOOT_STAGE_MIDI_READY            0x06U
#define APP_BOOT_STAGE_DISPLAY_READY         0x07U
#define APP_BOOT_STAGE_SPLASH_DONE           0x08U
#define APP_BOOT_STAGE_CV_READY              0x09U
#define APP_BOOT_STAGE_NET_READY             0x0AU
#define APP_BOOT_STAGE_OSC_READY             0x0BU
#define APP_BOOT_STAGE_MAIN_LOOP             0x0CU

static void app_boot_mark(uint32_t stage)
{
	app_boot_stage = stage;
	app_boot_error_code = 0;
}

static void app_boot_mark_error(uint32_t stage, int err)
{
	app_boot_stage = stage;
	app_boot_error_code = err;
}

#if defined(CONFIG_NETWORKING)
#define APP_NET_EVENT_MASK (NET_EVENT_IF_UP | NET_EVENT_IF_DOWN | \
			    NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED | \
			    NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL)

static struct net_mgmt_event_callback app_net_mgmt_cb;
static int app_osc_sock_fd = -1;
static struct sockaddr_in app_osc_remote_addr;
static bool app_osc_remote_addr_ready;
static bool app_osc_sock_bound;
static volatile bool app_net_indicator_state;
static volatile bool app_net_indicator_pending;
static volatile bool app_net_activity_pending;
static bool app_net_last_link_up;
static uint32_t app_net_next_poll_ms;
static uint32_t app_net_next_activity_blink_ms;

static bool app_network_ready_for_tx(void)
{
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		return false;
	}

	if (!net_if_is_up(iface)) {
		return false;
	}

	if (net_if_oper_state(iface) != NET_IF_OPER_UP) {
		return false;
	}

	if (!net_if_is_carrier_ok(iface)) {
		return false;
	}

	return net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) != NULL;
}

static bool app_network_link_up(struct net_if *iface)
{
	if (!iface) {
		iface = net_if_get_default();
	}

	if (!iface) {
		return false;
	}

	if (!net_if_is_up(iface)) {
		return false;
	}

	return net_if_is_carrier_ok(iface);
}

static void app_network_indicator_update(struct net_if *iface)
{
	app_net_indicator_state = app_network_link_up(iface);
	app_net_indicator_pending = true;
}

static void app_network_activity_mark(void)
{
	app_net_activity_pending = true;
}

static bool app_network_activity_service(uint32_t now_ms)
{
	if (!app_net_activity_pending) {
		return false;
	}

	if ((int32_t)(now_ms - app_net_next_activity_blink_ms) < 0) {
		return false;
	}

	app_net_activity_pending = false;
	asynth_display_act_n();
	app_net_next_activity_blink_ms = now_ms + NET_ACTIVITY_BLINK_MIN_INTERVAL_MS;

	return true;
}

static int app_osc_send_packet(const char *packet,
				       size_t packet_len,
				       const struct sockaddr *target_addr,
				       socklen_t target_len)
{
	int ret;

	ret = zsock_sendto(app_osc_sock_fd,
			 packet,
			 packet_len,
			 0,
			 target_addr,
			 target_len);
	if (ret < 0) {
		return -errno;
	}

	app_network_activity_mark();
	return 0;
}

static uint16_t app_osc_target_port(void)
{
	return (uint16_t)(NET_SETTINGS_PORT_BASE + net_cfg.target_port);
}

static uint16_t app_osc_device_port(void)
{
	return (uint16_t)(NET_SETTINGS_PORT_BASE + net_cfg.device_port);
}

static int app_osc_update_remote_addr(void)
{
	char ip_str[24];
	uint16_t port = app_osc_target_port();
	int ret;

	snprintk(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
		 (unsigned int)net_cfg.ip_b1,
		 (unsigned int)net_cfg.ip_b2,
		 (unsigned int)net_cfg.ip_b3,
		 (unsigned int)net_cfg.target_ip4);

	memset(&app_osc_remote_addr, 0, sizeof(app_osc_remote_addr));
	app_osc_remote_addr.sin_family = AF_INET;
	app_osc_remote_addr.sin_port = htons(port);

	ret = zsock_inet_pton(AF_INET, ip_str, &app_osc_remote_addr.sin_addr);
	if (ret != 1) {
		printk("OSC: invalid target IP %s\n", ip_str);
		app_osc_remote_addr_ready = false;
		return -EINVAL;
	}

	app_osc_remote_addr_ready = true;
	return 0;
}

static int app_osc_ensure_socket_and_target(void)
{
	struct sockaddr_in bind_addr;
	int ret;

	if (app_osc_sock_fd < 0) {
		app_osc_sock_fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (app_osc_sock_fd < 0) {
			printk("OSC: socket create failed (errno=%d)\n", errno);
			return -errno;
		}
	}

	if (!app_osc_sock_bound) {
		memset(&bind_addr, 0, sizeof(bind_addr));
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_port = htons(app_osc_device_port());
		bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

		ret = zsock_bind(app_osc_sock_fd,
					 (struct sockaddr *)&bind_addr,
					 sizeof(bind_addr));
		if (ret < 0) {
			ret = -errno;
			printk("OSC: bind failed on port %u (%d)\n",
			       (unsigned int)app_osc_device_port(), ret);
			(void)zsock_close(app_osc_sock_fd);
			app_osc_sock_fd = -1;
			return ret;
		}

		app_osc_sock_bound = true;
		printk("OSC RX: listening on UDP %u\n", (unsigned int)app_osc_device_port());
	}

	if (!app_osc_remote_addr_ready) {
		ret = app_osc_update_remote_addr();
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int app_osc_send_pong(const struct sockaddr_in *remote_addr, socklen_t remote_len)
{
	char packet[64];
	int len;
	int ret;
	const struct sockaddr_in *target_addr;
	socklen_t target_len;

	ARG_UNUSED(remote_addr);
	ARG_UNUSED(remote_len);

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	target_addr = &app_osc_remote_addr;
	target_len = sizeof(app_osc_remote_addr);

	len = tosc_writeMessage(packet, sizeof(packet), OSC_PATH_PONG, "");
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (const struct sockaddr *)target_addr,
				   target_len);
}

static bool app_osc_handle_rx_message(tosc_message *msg,
				      const struct sockaddr_in *remote_addr,
				      socklen_t remote_len)
{
	const char *address = tosc_getAddress(msg);
	const char *format = tosc_getFormat(msg);
	const char *text;
	int32_t cue_value;
	bool idle_enabled;
	float aout_value;
	int ret;

	if (address == NULL) {
		return false;
	}

	if (strcmp(address, OSC_PATH_PING) == 0) {
		ret = app_osc_send_pong(remote_addr, remote_len);
		if (ret < 0) {
			printk("OSC RX: /ping -> /pong failed (%d)\n", ret);
		}
		return false;
	}

	if (strcmp(address, OSC_PATH_IDLE) == 0) {
		if (!app_osc_parse_optional_bool(format, msg, &idle_enabled)) {
			printk("OSC RX: %s expects no arg, bool, or int(0/1)\n", address);
			return false;
		}

		app_idle_set(idle_enabled);
		return true;
	}

	if (strcmp(address, OSC_PATH_AOUT) == 0) {
		if ((format == NULL) || (format[1] != '\0')) {
			printk("OSC RX: /aout expects one normalized number\n");
			return false;
		}

		if (format[0] == 'f') {
			aout_value = tosc_getNextFloat(msg);
		} else if (format[0] == 'd') {
			aout_value = (float)tosc_getNextDouble(msg);
		} else if (format[0] == 'i') {
			aout_value = (float)tosc_getNextInt32(msg);
		} else {
			printk("OSC RX: /aout expects f, d, or i\n");
			return false;
		}

		ret = app_aout_set_normalized(aout_value);
		if (ret < 0) {
			printk("AOUT: write failed (%d)\n", ret);
		}

		return true;
	}

	if (strcmp(address, OSC_PATH_MSG) != 0) {
		if (strcmp(address, OSC_PATH_CUE) == 0) {
			if ((format == NULL) || (format[0] != 'i')) {
				printk("OSC RX: /cue expects OSC int32\n");
				return false;
			}

			cue_value = tosc_getNextInt32(msg);
			if (cue_value < 0) {
				cue_value = 0;
			}

			if (cue_value > 999) {
				cue_value = 999;
			}

			cue_pending_cancel(false);
			current_cue_value = (uint16_t)cue_value;
			asynth_display_print_cue(current_cue_value);

			if (net_settings_ready) {
				ret = settings_save_one(CUE_SETTINGS_KEY_CURRENT_VALUE,
						      &current_cue_value,
						      sizeof(current_cue_value));
				if (ret < 0) {
					printk("CUE: save failed (%d)\n", ret);
				}
			}

			return true;
		}

		printk("OSC RX: unhandled path '%s'\n", address);
		return false;
	}

	if ((format == NULL) || (format[0] != 's')) {
		printk("OSC RX: /msg expects OSC string\n");
		return false;
	}

	text = tosc_getNextString(msg);
	if (text == NULL) {
		text = "";
	}

	ret = asynth_display_print_msg(text);
	if (ret < 0) {
		printk("OSC RX: display print failed (%d)\n", ret);
		return false;
	}

	return true;
}

static bool app_osc_process_packet(char *packet,
				   int len,
				   const struct sockaddr_in *remote_addr,
				   socklen_t remote_len)
{
	tosc_message msg;
	tosc_bundle bundle;
	bool ui_dirty = false;
	int ret;

	if ((packet == NULL) || (len <= 0)) {
		return false;
	}

	app_network_activity_mark();

	if (tosc_isBundle(packet)) {
		tosc_parseBundle(&bundle, packet, len);
		while (tosc_getNextMessage(&bundle, &msg)) {
			if (app_osc_handle_rx_message(&msg, remote_addr, remote_len)) {
				ui_dirty = true;
			}
		}
		return ui_dirty;
	}

	ret = tosc_parseMessage(&msg, packet, len);
	if (ret < 0) {
		printk("OSC RX: parse failed (%d)\n", ret);
		return false;
	}

	if (app_osc_handle_rx_message(&msg, remote_addr, remote_len)) {
		ui_dirty = true;
	}

	return ui_dirty;
}

static bool app_osc_poll_rx(void)
{
	char packet[256];
	struct sockaddr_in remote_addr;
	socklen_t remote_len;
	int len;
	int ret;
	bool ui_dirty = false;
	uint8_t i;

	if (!app_network_ready_for_tx()) {
		return false;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return false;
	}

	for (i = 0U; i < 4U; i++) {
		remote_len = sizeof(remote_addr);
		len = zsock_recvfrom(app_osc_sock_fd,
				    packet,
				    sizeof(packet),
				    ZSOCK_MSG_DONTWAIT,
				    (struct sockaddr *)&remote_addr,
				    &remote_len);
		if (len < 0) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				break;
			}

			printk("OSC RX: recvfrom failed (%d)\n", errno);
			break;
		}

		if (len == 0) {
			continue;
		}

		if (app_osc_process_packet(packet, len, &remote_addr, remote_len)) {
			ui_dirty = true;
		}
	}

	return ui_dirty;
}

static void app_print_ipv4_status(struct net_if *iface)
{
	char addr_buf[NET_IPV4_ADDR_LEN];
	const struct in_addr *addr;

	if (!iface) {
		iface = net_if_get_default();
	}

	if (!iface) {
		printk("NET: no default interface\n");
		return;
	}

	addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (!addr) {
		printk("NET: IPv4 not assigned yet\n");
		return;
	}

	if (!net_addr_ntop(AF_INET, addr, addr_buf, sizeof(addr_buf))) {
		printk("NET: failed to format IPv4\n");
		return;
	}

	printk("NET: IPv4=%s\n", addr_buf);
}

static void app_net_event_handler(struct net_mgmt_event_callback *cb,
				  uint64_t mgmt_event,
				  struct net_if *iface)
{
	ARG_UNUSED(cb);

	if (mgmt_event == NET_EVENT_IF_UP) {
		printk("NET: interface up\n");
		app_network_indicator_update(iface);
		return;
	}

	if (mgmt_event == NET_EVENT_IF_DOWN) {
		printk("NET: interface down\n");
		app_network_indicator_update(iface);
		return;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		printk("NET: L4 connected\n");
		app_network_indicator_update(iface);
		app_print_ipv4_status(iface);
		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		printk("NET: L4 disconnected\n");
		app_network_indicator_update(iface);
		return;
	}

	if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
		printk("NET: IPv4 address added\n");
		app_network_indicator_update(iface);
		app_print_ipv4_status(iface);
		return;
	}

	if (mgmt_event == NET_EVENT_IPV4_ADDR_DEL) {
		printk("NET: IPv4 address removed\n");
		app_network_indicator_update(iface);
		return;
	}
}

static int app_network_init(void)
{
	struct net_if *iface;
	struct net_linkaddr *link_addr;
	int ret;

	iface = net_if_get_default();
	if (!iface) {
		printk("NET: no default interface available\n");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&app_net_mgmt_cb,
				     app_net_event_handler,
				     APP_NET_EVENT_MASK);
	net_mgmt_add_event_callback(&app_net_mgmt_cb);
	app_net_last_link_up = app_network_link_up(iface);
	app_net_next_poll_ms = k_uptime_get_32();
	app_net_indicator_state = app_net_last_link_up;
	app_net_indicator_pending = true;

	if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		conn_mgr_mon_resend_status();
	}

	link_addr = net_if_get_link_addr(iface);
	if (link_addr && link_addr->len == 6U) {
		printk("NET: MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
		       link_addr->addr[0], link_addr->addr[1], link_addr->addr[2],
		       link_addr->addr[3], link_addr->addr[4], link_addr->addr[5]);
	}

	ret = net_if_up(iface);
	if (ret < 0 && ret != -EALREADY) {
		printk("NET: net_if_up failed (%d)\n", ret);
		return ret;
	}

	printk("NET: Ethernet bring-up requested\n");
	app_print_ipv4_status(iface);

	return 0;
}

static int app_osc_transport_send_cv(uint8_t cv_index, float normalized)
{
	char path[20];
	char packet[128];
	int len;
	int ret;

	if (cv_index >= CV_CHANNEL_COUNT) {
		return -EINVAL;
	}

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	switch (cv_index) {
	case 0:
		snprintk(path, sizeof(path), OSC_PATH_CV1);
		break;
	case 1:
		snprintk(path, sizeof(path), OSC_PATH_CV2);
		break;
	case 2:
		snprintk(path, sizeof(path), OSC_PATH_CV3);
		break;
	case 3:
		snprintk(path, sizeof(path), OSC_PATH_CV4);
		break;
	default:
		return -EINVAL;
	}
	len = tosc_writeMessage(packet, sizeof(packet), path, "f", (double)normalized);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_transport_send_trigger(uint8_t trigger_index, bool active)
{
	char path[24];
	char packet[128];
	int len;
	int ret;

	if (trigger_index > 1U) {
		return -EINVAL;
	}

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	snprintk(path, sizeof(path), trigger_index == 0 ? OSC_PATH_TRIGGER1 : OSC_PATH_TRIGGER2);
	len = tosc_writeMessage(packet, sizeof(packet), path, active ? "T" : "F");
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_cue(uint16_t cue_value)
{
	char packet[128];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	len = tosc_writeMessage(packet, sizeof(packet), OSC_PATH_CUE, "i", (int32_t)cue_value);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_midi_note_on(uint8_t channel, uint8_t pitch, uint8_t velocity)
{
	char packet[128];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	/* Spec §5.1: /note <int channel> <int pitch> <int velocity>
	 * Channel exported as 1-16, pitch 0-127, velocity 1-127. */
	len = tosc_writeMessage(packet,
				 sizeof(packet),
				 OSC_PATH_MIDI_NOTE,
				 "iii",
				 (int32_t)(channel + 1U),
				 (int32_t)pitch,
				 (int32_t)velocity);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_midi_note_off(uint8_t channel, uint8_t pitch)
{
	char packet[128];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	/* Spec §5.2A: preferred note-off = velocity-zero /note message.
	 * /note <int channel> <int pitch> <int velocity=0> */
	len = tosc_writeMessage(packet,
				 sizeof(packet),
				 OSC_PATH_MIDI_NOTE,
				 "iii",
				 (int32_t)(channel + 1U),
				 (int32_t)pitch,
				 (int32_t)0);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_midi_pc(uint8_t channel, uint8_t program)
{
	char packet[128];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	/* Spec §7: /program <int channel> <int program>
	 * Channel exported as 1-16, program 0-127 (no +1 offset). */
	len = tosc_writeMessage(packet, sizeof(packet),
				OSC_PATH_MIDI_PC, "ii",
				(int32_t)(channel + 1U), (int32_t)program);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_midi_cc(uint8_t channel, uint8_t number, uint8_t value)
{
	char packet[128];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	/* Spec §6: /control <int channel> <int controller> <int value>
	 * Channel exported as 1-16, controller 0-127, value 0-127. */
	len = tosc_writeMessage(packet, sizeof(packet),
				OSC_PATH_MIDI_CC, "iii",
				(int32_t)(channel + 1U), (int32_t)number, (int32_t)value);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_midi_pitch_bend(uint8_t channel, uint16_t value)
{
	char packet[128];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	/* Spec §8: /pitch <int channel> <int value>
	 * Channel exported as 1-16, value 0-16383, 8192=center. */
	len = tosc_writeMessage(packet, sizeof(packet),
				OSC_PATH_MIDI_PITCH_BEND, "ii",
				(int32_t)(channel + 1U), (int32_t)value);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_no_arg(const char *path)
{
	char packet[64];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	len = tosc_writeMessage(packet, sizeof(packet), path, "");
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_midi_clock(void)
{
	/* Spec §11.1: /clock — no arguments, sent 24 times per quarter note. */
	return app_osc_send_no_arg(OSC_PATH_MIDI_CLOCK);
}

static int app_osc_send_midi_start(void)
{
	/* Spec §11.2: /start — no arguments. */
	return app_osc_send_no_arg(OSC_PATH_MIDI_START);
}

static int app_osc_send_midi_stop(void)
{
	/* Spec §11.2: /stop — no arguments. */
	return app_osc_send_no_arg(OSC_PATH_MIDI_STOP);
}

static int app_osc_send_midi_continue(void)
{
	/* Spec §11.2: /continue — no arguments. */
	return app_osc_send_no_arg(OSC_PATH_MIDI_CONTINUE);
}

static int app_osc_send_midi_songpos(uint16_t pos)
{
	char packet[64];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	/* Spec §11.3: /songpos <int value> — value in MIDI beats × 6 ticks. */
	len = tosc_writeMessage(packet, sizeof(packet),
				OSC_PATH_MIDI_SONGPOS, "i", (int32_t)pos);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_midi_mtc_qf(uint8_t piece, uint8_t value)
{
	char packet[64];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	/* /mtc_qf <int piece> <int value> — piece 0-7, value nibble 0-15. */
	len = tosc_writeMessage(packet, sizeof(packet),
				OSC_PATH_MIDI_MTC_QF, "ii",
				(int32_t)piece, (int32_t)value);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static int app_osc_send_midi_mtc_ff(uint8_t hour, uint8_t minute, uint8_t second,
				    uint8_t frame, uint8_t fps)
{
	char packet[128];
	int len;
	int ret;

	if (!app_network_ready_for_tx()) {
		return 0;
	}

	ret = app_osc_ensure_socket_and_target();
	if (ret < 0) {
		return ret;
	}

	/* Spec §12.1: /mtc <int hour> <int minute> <int second> <int frame> <int fps> */
	len = tosc_writeMessage(packet, sizeof(packet),
				OSC_PATH_MIDI_MTC_FF, "iiiii",
				(int32_t)hour, (int32_t)minute,
				(int32_t)second, (int32_t)frame, (int32_t)fps);
	if (len < 0) {
		return -EINVAL;
	}

	return app_osc_send_packet(packet,
				   (size_t)len,
				   (struct sockaddr *)&app_osc_remote_addr,
				   sizeof(app_osc_remote_addr));
}

static void app_osc_transport_configure(void)
{
	const struct asynth_osc_transport_ops ops = {
		.send_cv = app_osc_transport_send_cv,
		.send_trigger = app_osc_transport_send_trigger,
		.send_midi_note_on = app_osc_send_midi_note_on,
		.send_midi_note_off = app_osc_send_midi_note_off,
		.send_midi_pc = app_osc_send_midi_pc,
		.send_midi_cc = app_osc_send_midi_cc,
		.send_midi_pitch_bend = app_osc_send_midi_pitch_bend,
		.send_midi_clock = app_osc_send_midi_clock,
		.send_midi_start = app_osc_send_midi_start,
		.send_midi_stop = app_osc_send_midi_stop,
		.send_midi_continue = app_osc_send_midi_continue,
		.send_midi_songpos = app_osc_send_midi_songpos,
		.send_midi_mtc_qf = app_osc_send_midi_mtc_qf,
		.send_midi_mtc_ff = app_osc_send_midi_mtc_ff,
	};

	asynth_osc_set_transport_ops(&ops);
	app_osc_remote_addr_ready = false;
	asynth_osc_set_transport_enabled(true);
	printk("OSC: bridge transport callbacks enabled (UDP)\n");
}
#endif

#if !defined(CONFIG_NETWORKING)
static void app_osc_transport_configure(void)
{
	asynth_osc_set_transport_ops(NULL);
	asynth_osc_set_transport_enabled(false);
}

static int app_osc_send_cue(uint16_t cue_value)
{
	ARG_UNUSED(cue_value);
	return -ENOTSUP;
}

static bool app_osc_poll_rx(void)
{
	return false;
}

static int app_osc_send_midi_note_on(uint8_t channel, uint8_t pitch, uint8_t velocity)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(pitch);
	ARG_UNUSED(velocity);
	return -ENOTSUP;
}

static int app_osc_send_midi_note_off(uint8_t channel, uint8_t pitch)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(pitch);
	return -ENOTSUP;
}

static int app_osc_send_midi_pc(uint8_t channel, uint8_t program)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(program);
	return -ENOTSUP;
}

static int app_osc_send_midi_cc(uint8_t channel, uint8_t number, uint8_t value)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(number);
	ARG_UNUSED(value);
	return -ENOTSUP;
}

static int app_osc_send_midi_pitch_bend(uint8_t channel, uint16_t value)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(value);
	return -ENOTSUP;
}

static int app_osc_send_midi_clock(void)
{
	return -ENOTSUP;
}

static int app_osc_send_midi_start(void)
{
	return -ENOTSUP;
}

static int app_osc_send_midi_stop(void)
{
	return -ENOTSUP;
}

static int app_osc_send_midi_continue(void)
{
	return -ENOTSUP;
}

static int app_osc_send_midi_songpos(uint16_t pos)
{
	ARG_UNUSED(pos);
	return -ENOTSUP;
}

static int app_osc_send_midi_mtc_qf(uint8_t piece, uint8_t value)
{
	ARG_UNUSED(piece);
	ARG_UNUSED(value);
	return -ENOTSUP;
}

static int app_osc_send_midi_mtc_ff(uint8_t hour, uint8_t minute, uint8_t second,
				    uint8_t frame, uint8_t fps)
{
	ARG_UNUSED(hour);
	ARG_UNUSED(minute);
	ARG_UNUSED(second);
	ARG_UNUSED(frame);
	ARG_UNUSED(fps);
	return -ENOTSUP;
}
#endif

static const char *net_mode_to_str(uint16_t mode)
{
	switch (mode) {
	case 0U:
		return "Static";
	case 1U:
		return "DHCP+LL";
	case 2U:
		return "DHCP+FB";
	default:
		return "Unknown";
	}
}

static uint32_t net_cidr_to_mask(uint16_t cidr)
{
	if (cidr == 0U) {
		return 0U;
	}

	if (cidr >= 32U) {
		return 0xFFFFFFFFU;
	}

	return 0xFFFFFFFFU << (32U - cidr);
}

static void net_settings_clamp_all(void)
{
	if (net_cfg.ip_mode > NET_SETTINGS_IP_MODE_MAX) {
		net_cfg.ip_mode = NET_SETTINGS_IP_MODE_MAX;
	}

	if (net_cfg.target_ip4 > NET_SETTINGS_IP_BYTE_MAX) {
		net_cfg.target_ip4 = NET_SETTINGS_IP_BYTE_MAX;
	}

	if (net_cfg.target_port > NET_SETTINGS_PORT_OFFSET_MAX) {
		net_cfg.target_port = NET_SETTINGS_PORT_OFFSET_MAX;
	}

	if (net_cfg.device_ip4 > NET_SETTINGS_IP_BYTE_MAX) {
		net_cfg.device_ip4 = NET_SETTINGS_IP_BYTE_MAX;
	}

	if (net_cfg.device_port > NET_SETTINGS_PORT_OFFSET_MAX) {
		net_cfg.device_port = NET_SETTINGS_PORT_OFFSET_MAX;
	}

	if (net_cfg.cidr_mask > NET_SETTINGS_CIDR_MAX) {
		net_cfg.cidr_mask = NET_SETTINGS_CIDR_MAX;
	}

	if (net_cfg.ip_b1 > NET_SETTINGS_IP_BYTE_MAX) {
		net_cfg.ip_b1 = NET_SETTINGS_IP_BYTE_MAX;
	}

	if (net_cfg.ip_b2 > NET_SETTINGS_IP_BYTE_MAX) {
		net_cfg.ip_b2 = NET_SETTINGS_IP_BYTE_MAX;
	}

	if (net_cfg.ip_b3 > NET_SETTINGS_IP_BYTE_MAX) {
		net_cfg.ip_b3 = NET_SETTINGS_IP_BYTE_MAX;
	}

	if (cv_sample_period_ms < CV_SAMPLE_PERIOD_MIN_MS) {
		cv_sample_period_ms = CV_SAMPLE_PERIOD_MIN_MS;
	}

	if (cv_sample_period_ms > CV_SAMPLE_PERIOD_MAX_MS) {
		cv_sample_period_ms = CV_SAMPLE_PERIOD_MAX_MS;
	}

	if (cv_hysteresis_permille < CV_HYSTERESIS_MIN_PERMILLE) {
		cv_hysteresis_permille = CV_HYSTERESIS_MIN_PERMILLE;
	}

	if (cv_hysteresis_permille > CV_HYSTERESIS_MAX_PERMILLE) {
		cv_hysteresis_permille = CV_HYSTERESIS_MAX_PERMILLE;
	}

	if ((cv_hysteresis_permille % CV_HYSTERESIS_STEP_PERMILLE) != 0U) {
		uint16_t rounded = (uint16_t)(((cv_hysteresis_permille +
					      (CV_HYSTERESIS_STEP_PERMILLE / 2U)) /
					     CV_HYSTERESIS_STEP_PERMILLE) *
					    CV_HYSTERESIS_STEP_PERMILLE);

		if (rounded < CV_HYSTERESIS_MIN_PERMILLE) {
			rounded = CV_HYSTERESIS_MIN_PERMILLE;
		}

		if (rounded > CV_HYSTERESIS_MAX_PERMILLE) {
			rounded = CV_HYSTERESIS_MAX_PERMILLE;
		}

		cv_hysteresis_permille = rounded;
	}

	cv_hysteresis_norm = ((float)cv_hysteresis_permille) / 1000.0f;
}

static void midi_settings_apply(void)
{
	if (midi_note_thru > MIDI_SETTINGS_NOTE_THRU_MAX) {
		midi_note_thru = 0U;
	}

	asynth_midi_set_note_forward_enabled(midi_note_thru == 1U);
}

static int net_settings_read_u16(size_t len, settings_read_cb read_cb, void *cb_arg, uint16_t *dst)
{
	uint16_t tmp;
	int rc;

	if (len != sizeof(tmp)) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, &tmp, sizeof(tmp));
	if (rc < 0) {
		return rc;
	}

	if (rc != sizeof(tmp)) {
		return -EINVAL;
	}

	*dst = tmp;
	return 0;
}

static int asynth_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				      void *cb_arg)
{
	const char *next;
	uint16_t tmp;
	int ret;

	if (settings_name_steq(name, "adc/sample_period_ms", &next) && !next) {
		ret = net_settings_read_u16(len, read_cb, cb_arg, &tmp);
		if (ret < 0) {
			return ret;
		}

		cv_sample_period_ms = tmp;
		return 0;
	}

	if (settings_name_steq(name, "adc/hysteresis_permille", &next) && !next) {
		ret = net_settings_read_u16(len, read_cb, cb_arg, &tmp);
		if (ret < 0) {
			return ret;
		}

		cv_hysteresis_permille = tmp;
		return 0;
	}

	if (settings_name_steq(name, "midi/note_thru", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &midi_note_thru);
	}

	if (settings_name_steq(name, "net/ip_mode", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.ip_mode);
	}

	if (settings_name_steq(name, "net/target_ip4", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.target_ip4);
	}

	if (settings_name_steq(name, "net/target_port", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.target_port);
	}

	if (settings_name_steq(name, "net/device_ip4", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.device_ip4);
	}

	if (settings_name_steq(name, "net/device_port", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.device_port);
	}

	if (settings_name_steq(name, "net/cidr_mask", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.cidr_mask);
	}

	if (settings_name_steq(name, "net/ip_b1", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.ip_b1);
	}

	if (settings_name_steq(name, "net/ip_b2", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.ip_b2);
	}

	if (settings_name_steq(name, "net/ip_b3", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &net_cfg.ip_b3);
	}

	if (settings_name_steq(name, "cue/current_value", &next) && !next) {
		return net_settings_read_u16(len, read_cb, cb_arg, &current_cue_value);
	}

	return -ENOENT;
}

static void net_settings_log_current(const char *origin)
{
	uint32_t mask = net_cidr_to_mask(net_cfg.cidr_mask);
	uint16_t target_port = NET_SETTINGS_PORT_BASE + net_cfg.target_port;
	uint16_t device_port = NET_SETTINGS_PORT_BASE + net_cfg.device_port;

	printk("NETCFG[%s]: mode=%u (%s)\n",
	       origin,
	       (unsigned int)net_cfg.ip_mode,
	       net_mode_to_str(net_cfg.ip_mode));

	printk("NETCFG[%s]: dev=%u.%u.%u.%u/%u mask=%u.%u.%u.%u rx=%u\n",
	       origin,
	       (unsigned int)net_cfg.ip_b1,
	       (unsigned int)net_cfg.ip_b2,
	       (unsigned int)net_cfg.ip_b3,
	       (unsigned int)net_cfg.device_ip4,
	       (unsigned int)net_cfg.cidr_mask,
	       (unsigned int)((mask >> 24) & 0xFFU),
	       (unsigned int)((mask >> 16) & 0xFFU),
	       (unsigned int)((mask >> 8) & 0xFFU),
	       (unsigned int)(mask & 0xFFU),
	       (unsigned int)device_port);

	printk("NETCFG[%s]: tgt=%u.%u.%u.%u tx=%u\n",
	       origin,
	       (unsigned int)net_cfg.ip_b1,
	       (unsigned int)net_cfg.ip_b2,
	       (unsigned int)net_cfg.ip_b3,
	       (unsigned int)net_cfg.target_ip4,
	       (unsigned int)target_port);
}

static void adc_settings_log_current(const char *origin)
{
	uint16_t hyst_mv =
		(uint16_t)(((uint32_t)cv_hysteresis_permille * CV_ADC_FULL_SCALE_MV + 500U) / 1000U);

	printk("ADCCFG[%s]: sample_ms=%u hyst=%u/1000 (%u mV)\n",
	       origin,
	       (unsigned int)cv_sample_period_ms,
	       (unsigned int)cv_hysteresis_permille,
	       (unsigned int)hyst_mv);
}

static int __unused net_settings_init(void)
{
	int ret;

	ret = settings_subsys_init();
	if (ret < 0) {
		printk("NETCFG: settings_subsys_init failed (%d)\n", ret);
		return ret;
	}

	if (!net_settings_handler_registered) {
		ret = settings_register(&asynth_settings_handler);
		if (ret < 0) {
			printk("NETCFG: settings_register failed (%d)\n", ret);
			return ret;
		}
		net_settings_handler_registered = true;
	}

	ret = settings_load();
	if (ret < 0) {
		printk("NETCFG: settings_load failed (%d)\n", ret);
		return ret;
	}

	net_settings_clamp_all();
	midi_settings_apply();
	net_settings_ready = true;
	net_settings_log_current("startup");
	adc_settings_log_current("startup");
	printk("MIDICFG[startup]: note_thru=%u\n", (unsigned int)midi_note_thru);

	return 0;
}

static void net_settings_use_defaults(void)
{
	net_settings_clamp_all();
	midi_settings_apply();
	net_settings_ready = false;
	net_settings_log_current("defaults");
	adc_settings_log_current("defaults");
	printk("MIDICFG[defaults]: note_thru=%u\n", (unsigned int)midi_note_thru);
}

static uint16_t menu_wrap_u16_step(uint16_t value, uint16_t min_val, uint16_t max_val, int8_t direction)
{
	if (direction > 0) {
		if (value >= max_val) {
			return min_val;
		}
		return value + 1U;
	}

	if (value <= min_val) {
		return max_val;
	}

	return value - 1U;
}

static void net_settings_save_and_report(const char *key, const char *label, uint16_t value)
{
	int ret;

	if (net_settings_ready) {
		ret = settings_save_one(key, &value, sizeof(value));
		if (ret < 0) {
			printk("NETCFG: save failed for %s (%d)\n", label, ret);
		}
	}
	printk("NETCFG: %s=%u\n", label, (unsigned int)value);
	net_settings_log_current("menu");
}

static void adc_settings_save_and_report(const char *key, const char *label, uint16_t value)
{
	int ret;

	if (net_settings_ready) {
		ret = settings_save_one(key, &value, sizeof(value));
		if (ret < 0) {
			printk("ADCCFG: save failed for %s (%d)\n", label, ret);
		}
	}

	printk("ADCCFG: %s=%u\n", label, (unsigned int)value);
	adc_settings_log_current("menu");
}

static void midi_settings_save_and_report(uint16_t value)
{
	int ret;

	if (net_settings_ready) {
		ret = settings_save_one(MIDI_SETTINGS_KEY_NOTE_THRU, &value, sizeof(value));
		if (ret < 0) {
			printk("MIDICFG: save failed for note_thru (%d)\n", ret);
		}
	}

	printk("MIDICFG: note_thru=%u\n", (unsigned int)value);
}


/*
* @brief Initialize led's GPIO
* @param structure gpio_dt_spec
* @return 0 on success, log errors otherwise
*/
int init_led(struct gpio_dt_spec led1)
{
	int ret;

	ret = gpio_is_ready_dt(&led1);
	if (led1.port && !ret) {
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
		}
	}

	return ret;
}
static int triggers_init(void)
{
	int ret;
	int level;

	if (!device_is_ready(trigger_1.port) || !device_is_ready(trigger_2.port)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&trigger_1, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&trigger_2, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	level = asynth_ui_pin_get_raw_dt(&trigger_1);
	if (level < 0) {
		return level;
	}
	trigger_1_prev_state = asynth_ui_level_is_pressed(&trigger_1, level);

	level = asynth_ui_pin_get_raw_dt(&trigger_2);
	if (level < 0) {
		return level;
	}
	trigger_2_prev_state = asynth_ui_level_is_pressed(&trigger_2, level);

	return 0;
}

/* Center bottom label is the mode indicator and center-button action hint. */
static void ui_write_center_mode_hint(void)
{
	cfb_framebuffer_set_font(oled, 0);
	cfb_set_kerning(oled, 0);
	if (current_mode == APP_MODE_MENU) {
		cfb_print(oled, UI_MODE_LABEL_MENU, UI_CENTER_X, UI_BUTTON_LABEL_Y);
	} else {
		cfb_print(oled, UI_MODE_LABEL_NORMAL, UI_CENTER_X, UI_BUTTON_LABEL_Y);
	}
}

static void ui_write_status_line(void)
{
	uint16_t value;

	if (current_mode == APP_MODE_MENU) {
		switch (current_menu_item) {
		case MENU_ITEM_SAMPLE_PERIOD:
			asynth_display_print_msg("CV period (ms)");
			asynth_display_print_cue(cv_sample_period_ms);
			break;
		case MENU_ITEM_HYSTERESIS:
			asynth_display_print_msg("CV Hysteresis (mV)");
			value = (uint16_t)(((uint32_t)cv_hysteresis_permille * CV_ADC_FULL_SCALE_MV + 500U) /
					   1000U);
			asynth_display_print_cue(value);
			break;
		case MENU_ITEM_NET_IP_MODE:
			asynth_display_print_msg("IP Mode");
			asynth_display_print_cue(net_cfg.ip_mode);
			break;
		case MENU_ITEM_NET_TARGET_IP4:
			asynth_display_print_msg("Target IP");
			asynth_display_print_cue(net_cfg.target_ip4);
			break;
		case MENU_ITEM_NET_TARGET_PORT:
			asynth_display_print_msg("Target Port");
			asynth_display_print_cue(net_cfg.target_port);
			break;
		case MENU_ITEM_NET_DEVICE_IP4:
			asynth_display_print_msg("Device IP");
			asynth_display_print_cue(net_cfg.device_ip4);
			break;
		case MENU_ITEM_NET_DEVICE_PORT:
			asynth_display_print_msg("Device Port");
			asynth_display_print_cue(net_cfg.device_port);
			break;
		case MENU_ITEM_NET_CIDR_MASK:
			asynth_display_print_msg("CIDR Mask");
			asynth_display_print_cue(net_cfg.cidr_mask);
			break;
		case MENU_ITEM_NET_IP_B1:
			asynth_display_print_msg("IP byte 1 ");
			asynth_display_print_cue(net_cfg.ip_b1);
			break;
		case MENU_ITEM_NET_IP_B2:
			asynth_display_print_msg("IP byte 2 ");
			asynth_display_print_cue(net_cfg.ip_b2);
			break;
		case MENU_ITEM_NET_IP_B3:
			asynth_display_print_msg("IP byte 3 ");
			asynth_display_print_cue(net_cfg.ip_b3);
			break;
		case MENU_ITEM_MIDI_NOTE_THRU:
			asynth_display_print_msg("MIDI Note (0:ignore 1:thru)");
			asynth_display_print_cue(midi_note_thru);
			break;
		default:
		// Should not happen, but clear the area if it does.
			asynth_display_print_msg("");
			asynth_display_print_cue(0);
			break;
		}
	} else {
		// In normal mode, the status line is reserved for transient messages, so clear it when writing other info to avoid confusion.
		asynth_display_print_msg("     ");
	}
}

static void ui_apply_button_visual_state(void)
{
	/* Visual state inversion removed - buttons no longer show pressed state */
	/* Keep this function as a no-op for now in case we need it later */
}

static void ui_enter_menu_mode(void)
{
	current_mode = APP_MODE_MENU;
	current_menu_item = MENU_ITEM_NET_IP_MODE;
	ui_write_center_mode_hint();
	ui_write_status_line();
	ui_apply_button_visual_state();
}

static void ui_exit_menu_mode(void)
{
	current_mode = APP_MODE_NORMAL;
	ui_write_center_mode_hint();
	ui_write_status_line();
	asynth_display_print_cue(current_cue_value);
	ui_apply_button_visual_state();

#if defined(CONFIG_NETWORKING)
	if (asynth_osc_is_transport_enabled()) {
		int ret;

		app_osc_remote_addr_ready = false;
		ret = app_osc_update_remote_addr();
		if (ret < 0) {
			printk("OSC: target update failed (%d)\n", ret);
		} else {
			printk("OSC: target=%u.%u.%u.%u:%u\n",
			       (unsigned int)net_cfg.ip_b1,
			       (unsigned int)net_cfg.ip_b2,
			       (unsigned int)net_cfg.ip_b3,
			       (unsigned int)net_cfg.target_ip4,
			       (unsigned int)app_osc_target_port());
		}
	}
#endif
}

static void cue_send_recall(uint16_t cue_value)
{
	int ret;

	ret = app_osc_send_cue(cue_value);
	if (ret < 0 && ret != -ENOTSUP) {
		printk("OSC send Cue failed (%d)\n", ret);
	}
}

static void printTempCueBlink(uint16_t cue_value, bool visible)
{
	if (visible) {
		asynth_display_print_cue(cue_value);
	} else {
		asynth_display_clear_cue();
	}
}

static void cue_pending_cancel(bool show_status)
{
	if (!cue_pending_active) {
		return;
	}

	cue_pending_active = false;
	cue_pending_visible = true;
	asynth_display_print_cue(current_cue_value);

	if (show_status) {
		// time out cancel, do nothing. 
		//asynth_display_print_msg("Cue cancel");
	}
}

static void cue_pending_commit_or_recall(void)
{
	if (cue_pending_active) {
		current_cue_value = cue_pending_value;
		cue_pending_active = false;
		cue_pending_visible = true;
		asynth_display_print_cue(current_cue_value);

		if (net_settings_ready) {
			int ret = settings_save_one(CUE_SETTINGS_KEY_CURRENT_VALUE, &current_cue_value, sizeof(current_cue_value));
			if (ret < 0) {
				printk("CUE: save failed (%d)\n", ret);
			}
		}
	}

	cue_send_recall(current_cue_value);
}

static void cue_apply_pending_edit_step(int8_t direction, uint32_t now_ms)
{
	if (direction == 0) {
		return;
	}

	if (!cue_pending_active) {
		cue_pending_active = true;
		cue_pending_value = current_cue_value;
	}

	if (direction > 0) {
		if (cue_pending_value >= 999U) {
			cue_pending_value = 0U;
		} else {
			cue_pending_value++;
		}
	} else {
		if (cue_pending_value == 0U) {
			cue_pending_value = 999U;
		} else {
			cue_pending_value--;
		}
	}

	cue_pending_visible = true;
	cue_pending_last_change_ms = now_ms;
	cue_pending_last_blink_ms = now_ms;
	printTempCueBlink(cue_pending_value, true);
}

static bool cue_pending_tick(uint32_t now_ms)
{
	if (!cue_pending_active || current_mode != APP_MODE_NORMAL) {
		return false;
	}

	if ((uint32_t)(now_ms - cue_pending_last_change_ms) >= CUE_PENDING_TIMEOUT_MS) {
		cue_pending_cancel(true);
		return true;
	}

	if ((uint32_t)(now_ms - cue_pending_last_blink_ms) >= CUE_PENDING_BLINK_INTERVAL_MS) {
		cue_pending_last_blink_ms = now_ms;
		cue_pending_visible = !cue_pending_visible;
		printTempCueBlink(cue_pending_value, cue_pending_visible);
		return true;
	}

	return false;
}

static void menu_apply_edit_step(int8_t direction)
{
	if (direction == 0) {
		return;
	}

	if (current_menu_item == MENU_ITEM_SAMPLE_PERIOD) {
		if (direction > 0) {
			cv_sample_period_ms += CV_SAMPLE_PERIOD_STEP_MS;
			if (cv_sample_period_ms > CV_SAMPLE_PERIOD_MAX_MS) {
				cv_sample_period_ms = CV_SAMPLE_PERIOD_MIN_MS;
			}
		} else {
			if (cv_sample_period_ms <= CV_SAMPLE_PERIOD_MIN_MS) {
				cv_sample_period_ms = CV_SAMPLE_PERIOD_MAX_MS;
			} else {
				cv_sample_period_ms -= CV_SAMPLE_PERIOD_STEP_MS;
			}
		}

		adc_settings_save_and_report(ADC_SETTINGS_KEY_SAMPLE_PERIOD_MS,
						"sample_ms",
						(uint16_t)cv_sample_period_ms);
	} else if (current_menu_item == MENU_ITEM_HYSTERESIS) {
		if (direction > 0) {
			cv_hysteresis_permille += CV_HYSTERESIS_STEP_PERMILLE;
			if (cv_hysteresis_permille > CV_HYSTERESIS_MAX_PERMILLE) {
				cv_hysteresis_permille = CV_HYSTERESIS_MIN_PERMILLE;
			}
		} else {
			if (cv_hysteresis_permille <= CV_HYSTERESIS_MIN_PERMILLE) {
				cv_hysteresis_permille = CV_HYSTERESIS_MAX_PERMILLE;
			} else {
				cv_hysteresis_permille -= CV_HYSTERESIS_STEP_PERMILLE;
			}
		}

		cv_hysteresis_norm = ((float)cv_hysteresis_permille) / 1000.0f;
		adc_settings_save_and_report(ADC_SETTINGS_KEY_HYSTERESIS_PERMILLE,
						"hyst_permille",
						cv_hysteresis_permille);
	} else if (current_menu_item == MENU_ITEM_NET_IP_MODE) {
		net_cfg.ip_mode = menu_wrap_u16_step(net_cfg.ip_mode, 0U, NET_SETTINGS_IP_MODE_MAX,
						   direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_IP_MODE, "IP mode", net_cfg.ip_mode);
	} else if (current_menu_item == MENU_ITEM_NET_TARGET_IP4) {
		net_cfg.target_ip4 = menu_wrap_u16_step(net_cfg.target_ip4, 0U, NET_SETTINGS_IP_BYTE_MAX,
						      direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_TARGET_IP4, "T IP", net_cfg.target_ip4);
	} else if (current_menu_item == MENU_ITEM_NET_TARGET_PORT) {
		net_cfg.target_port = menu_wrap_u16_step(net_cfg.target_port, 0U,
						NET_SETTINGS_PORT_OFFSET_MAX, direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_TARGET_PORT, "T port", net_cfg.target_port);
	} else if (current_menu_item == MENU_ITEM_NET_DEVICE_IP4) {
		net_cfg.device_ip4 = menu_wrap_u16_step(net_cfg.device_ip4, 0U, NET_SETTINGS_IP_BYTE_MAX,
						      direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_DEVICE_IP4, "D IP", net_cfg.device_ip4);
	} else if (current_menu_item == MENU_ITEM_NET_DEVICE_PORT) {
		net_cfg.device_port = menu_wrap_u16_step(net_cfg.device_port, 0U,
						NET_SETTINGS_PORT_OFFSET_MAX, direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_DEVICE_PORT, "D port", net_cfg.device_port);
	} else if (current_menu_item == MENU_ITEM_NET_CIDR_MASK) {
		net_cfg.cidr_mask = menu_wrap_u16_step(net_cfg.cidr_mask, 0U, NET_SETTINGS_CIDR_MAX,
						      direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_CIDR_MASK, "CIDR", net_cfg.cidr_mask);
	} else if (current_menu_item == MENU_ITEM_NET_IP_B1) {
		net_cfg.ip_b1 = menu_wrap_u16_step(net_cfg.ip_b1, 0U, NET_SETTINGS_IP_BYTE_MAX,
						  direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_IP_B1, "IP b1", net_cfg.ip_b1);
	} else if (current_menu_item == MENU_ITEM_NET_IP_B2) {
		net_cfg.ip_b2 = menu_wrap_u16_step(net_cfg.ip_b2, 0U, NET_SETTINGS_IP_BYTE_MAX,
						  direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_IP_B2, "IP b2", net_cfg.ip_b2);
	} else if (current_menu_item == MENU_ITEM_NET_IP_B3) {
		net_cfg.ip_b3 = menu_wrap_u16_step(net_cfg.ip_b3, 0U, NET_SETTINGS_IP_BYTE_MAX,
						  direction);
		net_settings_save_and_report(NET_SETTINGS_KEY_IP_B3, "IP b3", net_cfg.ip_b3);
	} else if (current_menu_item == MENU_ITEM_MIDI_NOTE_THRU) {
		midi_note_thru = menu_wrap_u16_step(midi_note_thru, 0U,
						   MIDI_SETTINGS_NOTE_THRU_MAX,
						   direction);
		midi_settings_apply();
		midi_settings_save_and_report(midi_note_thru);
	}

	ui_write_status_line();
}

static void menu_reset_current_item_to_default(void)
{
	if (current_menu_item == MENU_ITEM_SAMPLE_PERIOD) {
		cv_sample_period_ms = CV_SAMPLE_PERIOD_DEFAULT_MS;
		adc_settings_save_and_report(ADC_SETTINGS_KEY_SAMPLE_PERIOD_MS,
						"sample_ms",
						(uint16_t)cv_sample_period_ms);
	} else if (current_menu_item == MENU_ITEM_HYSTERESIS) {
		cv_hysteresis_permille = CV_HYSTERESIS_DEFAULT_PERMILLE;
		cv_hysteresis_norm = ((float)cv_hysteresis_permille) / 1000.0f;
		adc_settings_save_and_report(ADC_SETTINGS_KEY_HYSTERESIS_PERMILLE,
						"hyst_permille",
						cv_hysteresis_permille);
	} else if (current_menu_item == MENU_ITEM_NET_IP_MODE) {
		net_cfg.ip_mode = NET_SETTINGS_DEFAULT_IP_MODE;
		net_settings_save_and_report(NET_SETTINGS_KEY_IP_MODE, "IP mode", net_cfg.ip_mode);
	} else if (current_menu_item == MENU_ITEM_NET_TARGET_IP4) {
		net_cfg.target_ip4 = NET_SETTINGS_DEFAULT_TARGET_IP4;
		net_settings_save_and_report(NET_SETTINGS_KEY_TARGET_IP4, "T IP", net_cfg.target_ip4);
	} else if (current_menu_item == MENU_ITEM_NET_TARGET_PORT) {
		net_cfg.target_port = NET_SETTINGS_DEFAULT_TARGET_PORT;
		net_settings_save_and_report(NET_SETTINGS_KEY_TARGET_PORT, "T port", net_cfg.target_port);
	} else if (current_menu_item == MENU_ITEM_NET_DEVICE_IP4) {
		net_cfg.device_ip4 = NET_SETTINGS_DEFAULT_DEVICE_IP4;
		net_settings_save_and_report(NET_SETTINGS_KEY_DEVICE_IP4, "D IP", net_cfg.device_ip4);
	} else if (current_menu_item == MENU_ITEM_NET_DEVICE_PORT) {
		net_cfg.device_port = NET_SETTINGS_DEFAULT_DEVICE_PORT;
		net_settings_save_and_report(NET_SETTINGS_KEY_DEVICE_PORT, "D port", net_cfg.device_port);
	} else if (current_menu_item == MENU_ITEM_NET_CIDR_MASK) {
		net_cfg.cidr_mask = NET_SETTINGS_DEFAULT_CIDR_MASK;
		net_settings_save_and_report(NET_SETTINGS_KEY_CIDR_MASK, "CIDR", net_cfg.cidr_mask);
	} else if (current_menu_item == MENU_ITEM_NET_IP_B1) {
		net_cfg.ip_b1 = NET_SETTINGS_DEFAULT_IP_B1;
		net_settings_save_and_report(NET_SETTINGS_KEY_IP_B1, "IP b1", net_cfg.ip_b1);
	} else if (current_menu_item == MENU_ITEM_NET_IP_B2) {
		net_cfg.ip_b2 = NET_SETTINGS_DEFAULT_IP_B2;
		net_settings_save_and_report(NET_SETTINGS_KEY_IP_B2, "IP b2", net_cfg.ip_b2);
	} else if (current_menu_item == MENU_ITEM_NET_IP_B3) {
		net_cfg.ip_b3 = NET_SETTINGS_DEFAULT_IP_B3;
		net_settings_save_and_report(NET_SETTINGS_KEY_IP_B3, "IP b3", net_cfg.ip_b3);
	} else if (current_menu_item == MENU_ITEM_MIDI_NOTE_THRU) {
		midi_note_thru = 0U;
		midi_settings_apply();
		midi_settings_save_and_report(midi_note_thru);
	}

	ui_write_status_line();
}

static void cue_apply_edit_step(int8_t direction)
{
	if (direction > 0) {
		if (current_cue_value >= 999U) {
			current_cue_value = 0U;
		} else {
			current_cue_value++;
		}
	} else if (direction < 0) {
		if (current_cue_value == 0U) {
			current_cue_value = 999U;
		} else {
			current_cue_value--;
		}
	}

	if (net_settings_ready) {
		int ret = settings_save_one(CUE_SETTINGS_KEY_CURRENT_VALUE, &current_cue_value, sizeof(current_cue_value));
		if (ret < 0) {
			printk("CUE: save failed (%d)\n", ret);
		}
	}

	asynth_display_print_cue(current_cue_value);
}

static bool process_button_events(void)
{
	bool left_pressed_now;
	bool right_pressed_now;
	bool rot_button_state_now;
	int8_t delta;
	bool mode_switched_this_cycle = false;
	bool ui_dirty = false;
	uint32_t now_ms = k_uptime_get_32();

	if (asynth_ui_input_pop_button_event(ASYNTH_UI_BUTTON_LEFT, &left_pressed_now)) {
		if (!left_pressed_now) {
			if (current_mode == APP_MODE_MENU) {
				if (current_menu_item == 0U) {
					current_menu_item = (uint8_t)(MENU_ITEM_COUNT - 1U);
				} else {
					current_menu_item--;
				}
				ui_write_status_line();
				ui_dirty = true;
			} else if (current_mode == APP_MODE_NORMAL) {
				/* Prev button: decrement Cue */
				cue_pending_cancel(false);
				cue_apply_edit_step(-1);
				cue_send_recall(current_cue_value);
				ui_dirty = true;
			}
		}
	}

	if (asynth_ui_input_pop_button_event(ASYNTH_UI_BUTTON_RIGHT, &right_pressed_now)) {
		if (right_pressed_now) {
			app_idle_clear_on_press();
		}

		if (!right_pressed_now) {
			if (current_mode == APP_MODE_MENU) {
				current_menu_item = (uint8_t)((current_menu_item + 1U) % MENU_ITEM_COUNT);
				ui_write_status_line();
				ui_dirty = true;
			} else if (current_mode == APP_MODE_NORMAL) {
				/* Next button: increment Cue */
				cue_pending_cancel(false);
				cue_apply_edit_step(1);
				cue_send_recall(current_cue_value);
				ui_dirty = true;
			}
		}
	}

	/* Rotary button: debounced polling-based edge detection for reliable timing. */
	(void)asynth_ui_input_get_rot_button_debounced(now_ms, &rot_button_state_now);

	/* Detect press edge (false → true). */
	if (rot_button_state_now && !rot_button_was_pressed_last_poll) {
		if ((current_mode == APP_MODE_NORMAL) || (current_mode == APP_MODE_MENU)) {
			rot_press_start_ms = now_ms;
			rot_press_tracking = true;
			ui_dirty = true;
		}
	}

	/* Detect release edge (true → false). */
	if (!rot_button_state_now && rot_button_was_pressed_last_poll) {
		if (rot_press_tracking) {
			rot_press_tracking = false;
			if (current_mode == APP_MODE_NORMAL) {
				cue_pending_commit_or_recall();
			} else if (current_mode == APP_MODE_MENU) {
				menu_reset_current_item_to_default();
			}
		}
		ui_dirty = true;
	}

	/* Always cancel pending hold when released. */
	if (rot_press_tracking && !rot_button_state_now) {
		rot_press_tracking = false;
	}

	/* Check for continuous 2-second hold to switch mode. */
	if (rot_press_tracking && rot_button_state_now) {
		if ((uint32_t)(now_ms - rot_press_start_ms) >= ROTARY_LONG_PRESS_MS) {
			if (current_mode == APP_MODE_NORMAL) {
				cue_pending_cancel(false);
				ui_enter_menu_mode();
			} else if (current_mode == APP_MODE_MENU) {
				ui_exit_menu_mode();
			}
			rot_press_tracking = false;
			mode_switched_this_cycle = true;
			ui_dirty = true;
		}
	}

	/* Save current state for next poll cycle. */
	rot_button_was_pressed_last_poll = rot_button_state_now;

	if (mode_switched_this_cycle) {
		(void)asynth_ui_input_pop_rotary_delta();
		rot_encoder_step_accum = 0;
		return ui_dirty;
	}

	delta = asynth_ui_input_pop_rotary_delta();
	if (delta != 0) {
		rot_encoder_step_accum += delta;

		if (current_mode == APP_MODE_MENU) {
			while (rot_encoder_step_accum >= ROTARY_DETENT_STEPS) {
				menu_apply_edit_step(1);
				rot_encoder_step_accum -= ROTARY_DETENT_STEPS;
				ui_dirty = true;
			}

			while (rot_encoder_step_accum <= -ROTARY_DETENT_STEPS) {
				menu_apply_edit_step(-1);
				rot_encoder_step_accum += ROTARY_DETENT_STEPS;
				ui_dirty = true;
			}
		} else if (current_mode == APP_MODE_NORMAL) {
			while (rot_encoder_step_accum >= ROTARY_DETENT_STEPS) {
				cue_apply_pending_edit_step(1, now_ms);
				rot_encoder_step_accum -= ROTARY_DETENT_STEPS;
				ui_dirty = true;
			}

			while (rot_encoder_step_accum <= -ROTARY_DETENT_STEPS) {
				cue_apply_pending_edit_step(-1, now_ms);
				rot_encoder_step_accum += ROTARY_DETENT_STEPS;
				ui_dirty = true;
			}
		}
	}

	return ui_dirty;
}

static bool process_trigger_events(void)
{
	int level;
	int ret;
	bool state;
	bool ui_dirty = false;

	level = asynth_ui_pin_get_raw_dt(&trigger_1);
	if (level >= 0) {
		state = asynth_ui_level_is_pressed(&trigger_1, level);
		if (state != trigger_1_prev_state) {
			trigger_1_prev_state = state;
			asynth_display_act_t1();
			ret = asynth_osc_send_trigger(0U, state);
			if (ret < 0) {
				printk("OSC send Trigger1 failed (%d)\n", ret);
			}
			TRIGGER_EVENT_LOG("Trigger 1 %s\n", state ? "ON" : "OFF");
			ui_dirty = true;
		}
	}

	level = asynth_ui_pin_get_raw_dt(&trigger_2);
	if (level >= 0) {
		state = asynth_ui_level_is_pressed(&trigger_2, level);
		if (state != trigger_2_prev_state) {
			trigger_2_prev_state = state;
			asynth_display_act_t2();
			ret = asynth_osc_send_trigger(1U, state);
			if (ret < 0) {
				printk("OSC send Trigger2 failed (%d)\n", ret);
			}
			TRIGGER_EVENT_LOG("Trigger 2 %s\n", state ? "ON" : "OFF");
			ui_dirty = true;
		}
	}

	return ui_dirty;
}

FUNC_NORETURN static void app_fatal_stop(const char *stage, int err)
{
	app_boot_mark_error(app_boot_stage, err);
	printk("APP FATAL: %s (%d)\n", stage, err);
	while (1) {
		k_sleep(K_MSEC(1000));
	}
}

/**
 * Main loop
 * 
 * "Bare metal code" for screen updates and configuration menu.
 *
 */
int main(void)
{
	uint16_t x_res;
	uint16_t y_res;
	uint16_t rows;
	uint8_t ppt;
	uint8_t font_width;
	uint8_t font_height;
	int ret;
	int64_t next_cv_sample_ms;
	uint32_t midi_diag_last_log_ms = 0U;
	const struct asynth_ui_input_pins ui_pins = {
		.left_button = &left_button,
		.right_button = &right_button,
		.rot_button = &rot_button,
		.rot_encoder_a = &rot_encoder_a,
		.rot_encoder_b = &rot_encoder_b,
	};

	app_boot_mark(APP_BOOT_STAGE_ENTER_MAIN);
	printk("APP MAIN START\n");

	// log the current build version in the console for easy reference
	printk("Asynth2OSC build: %s\n", ASYNTH_BUILD_ID);

	// Initialize top left right buttons leds
	init_led(left_led);
	init_led(right_led);
	// turn them off
	ret = gpio_pin_toggle_dt(&right_led);
	if (ret < 0) {
		printk("WARN: right LED toggle failed (%d)\n", ret);
	}
	app_boot_mark(APP_BOOT_STAGE_GPIO_READY);
	ret = gpio_pin_toggle_dt(&left_led);
	if (ret < 0) {
		printk("WARN: left LED toggle failed (%d)\n", ret);
	}

	ret = asynth_ui_input_init(&ui_pins, BUTTON_DEBOUNCE_MS);
	app_boot_mark(APP_BOOT_STAGE_UI_READY);
	if (ret < 0) {
		printk("UI input init failed (%d)\n", ret);
		app_fatal_stop("ui_input_init", ret);
	}

	ret = triggers_init();
	app_boot_mark(APP_BOOT_STAGE_TRIGGERS_READY);
	if (ret < 0) {
		printk("Trigger input init failed (%d)\n", ret);
		app_fatal_stop("triggers_init", ret);
	}

	app_boot_mark(APP_BOOT_STAGE_SETTINGS_READY);
	ret = net_settings_init();
	if (ret < 0) {
		printk("NETCFG: using defaults (%d)\n", ret);
		net_settings_use_defaults();
	}

	app_boot_mark(APP_BOOT_STAGE_MIDI_READY);
	ret = asynth_midi_init();
	if (ret < 0) {
		printk("MIDI IN: disabled (%d)\n", ret);
	}

	ret = app_aout_init();
	if (ret < 0) {
		printk("AOUT: disabled (%d)\n", ret);
	} else {
		printk("AOUT: ready on DAC1 channel %u\n", (unsigned int)AOUT_DAC_CHANNEL_ID);
	}

	// Intialize Oled screen
	oled = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(oled)) {
		app_fatal_stop("display_not_ready", -ENODEV);
	}

	if (display_set_pixel_format(oled, PIXEL_FORMAT_MONO10) != 0) {
		if (display_set_pixel_format(oled, PIXEL_FORMAT_MONO01) != 0) {
			app_fatal_stop("display_set_pixel_format", -EIO);
		}
	}

	if (cfb_framebuffer_init(oled)) {
		app_fatal_stop("cfb_framebuffer_init", -EIO);
	}

	ret = asynth_display_init(oled);
	if (ret < 0) {
		printk("Display helper init failed (%d)\n", ret);
		app_fatal_stop("asynth_display_init", ret);
	}
	app_boot_mark(APP_BOOT_STAGE_DISPLAY_READY);

	cfb_framebuffer_clear(oled, true);
	display_blanking_off(oled);

	x_res = cfb_get_display_parameter(oled, CFB_DISPLAY_WIDTH);
	y_res = cfb_get_display_parameter(oled, CFB_DISPLAY_HEIGHT);
	rows = cfb_get_display_parameter(oled, CFB_DISPLAY_ROWS);
	ppt = cfb_get_display_parameter(oled, CFB_DISPLAY_PPT);

	// Enumerate the fonts ids
	for (int idx = 0; idx < 42; idx++) {
		if (cfb_get_font_size(oled, idx, &font_width, &font_height)) {
			break;
		}
		cfb_framebuffer_set_font(oled, idx);
	}
	// Default font width spacing
	cfb_set_kerning(oled, 0);


	
	/*	OLED Screen tests & splash "start up  screen"
	* 
	*/
	//cfb_framebuffer_invert(oled);
	cfb_framebuffer_set_font(oled, 0);
	cfb_print(oled, "Asynth2OSC", 0, 0);
	cfb_print(oled, "RederoTech", 0, 16*1);
	cfb_print(oled, "inside", 0, 16 * 2);
	cfb_print(oled, ASYNTH_BUILD_ID, 0, 16 * 3);
	cfb_invert_area(oled, 0, 0, 128, 16*1);
	cfb_framebuffer_finalize(oled);
	k_sleep(K_MSEC(SPLASH_SCREEN_DURATION_MS));
	cfb_framebuffer_clear(oled, true);
	cfb_framebuffer_finalize(oled);
	app_boot_mark(APP_BOOT_STAGE_SPLASH_DONE);

	// draw four rectangles for CV level monitoring
	for (int i = 0; i < 4; i++)
	{
		struct cfb_position corner1;
		struct cfb_position corner2;

		corner1.x = 80+i*12;
		corner1.y = 16;
		corner2.x = 90+i*12;
		corner2.y = 48;

		cfb_draw_rect(oled, &corner1, &corner2);
	}

	// Draw text for MIDI and TRIGGERS monitoring
	cfb_framebuffer_set_font(oled, 0);
	cfb_set_kerning(oled, 2);
	cfb_print(oled, " 1MA2", 68, 0);
	asynth_display_set_a(false);

	// Init center mode hint and button visual states
	ui_write_center_mode_hint();
	ui_write_status_line();
	asynth_display_print_cue(current_cue_value);
	ui_apply_button_visual_state();

	// Invert whole display once for proper color scheme
	cfb_framebuffer_invert(oled);

	// LEFT RIGHT Prev and Next button design
	// Permanently invert those areas (white background) to figure left and right button
	cfb_framebuffer_set_font(oled, 0);
	cfb_set_kerning(oled, 0);
	cfb_print(oled, "Next", UI_RIGHT_X, UI_BUTTON_LABEL_Y);
	cfb_print(oled, "Prev", UI_LEFT_X, UI_BUTTON_LABEL_Y);
	cfb_invert_area(oled, UI_LEFT_X, UI_BUTTON_INVERT_Y, UI_BUTTON_WIDTH, UI_BUTTON_HEIGHT);
	cfb_invert_area(oled, UI_RIGHT_X, UI_BUTTON_INVERT_Y, UI_BUTTON_WIDTH, UI_BUTTON_HEIGHT);

	cfb_framebuffer_finalize(oled);

	ret = asynth_cv_init(oled);
	if (ret < 0) {
		app_fatal_stop("asynth_cv_init", ret);
	}
	app_boot_mark(APP_BOOT_STAGE_CV_READY);

#if defined(CONFIG_NETWORKING)
	ret = app_network_init();
	if (ret < 0) {
		printk("NET: initialization failed (%d)\n", ret);
	}
	app_boot_mark(APP_BOOT_STAGE_NET_READY);
	
	/* Initialize OSC remote address with static IPs for initial testing */
	ret = app_osc_update_remote_addr();
	if (ret == 0) {
		printk("OSC: initial target address configured: %u.%u.%u.%u:%u\n",
		       (unsigned int)net_cfg.ip_b1,
		       (unsigned int)net_cfg.ip_b2,
		       (unsigned int)net_cfg.ip_b3,
		       (unsigned int)net_cfg.target_ip4,
		       (unsigned int)app_osc_target_port());
	} else {
		printk("OSC: failed to configure target address (%d)\n", ret);
	}
#else
	printk("NET: networking is disabled in this build\n");
#endif

	app_osc_transport_configure();
	app_boot_mark(APP_BOOT_STAGE_OSC_READY);

	next_cv_sample_ms = k_uptime_get();
	idle_next_active = false;
	idle_next_led_on = false;
	idle_next_led_toggle_ms = k_uptime_get_32();
	app_net_activity_pending = false;
	app_net_next_activity_blink_ms = k_uptime_get_32();
	app_boot_mark(APP_BOOT_STAGE_MAIN_LOOP);

	/* 
	*	Main infinite loop
	*/

	while (1) {
		bool ui_dirty = process_button_events();
		uint32_t now_ms = k_uptime_get_32();
		uint32_t dropped;

		asynth_midi_poll_fallback();
		asynth_midi_sample_uart_errors();

		if (asynth_midi_process_events()) {
			ui_dirty = true;
		}

		if (app_osc_poll_rx()) {
			ui_dirty = true;
		}

#if defined(CONFIG_NETWORKING)
		if ((int32_t)(now_ms - app_net_next_poll_ms) >= 0) {
			bool link_up_now = app_network_link_up(NULL);

			app_net_next_poll_ms = now_ms + NET_LINK_POLL_INTERVAL_MS;
			if (link_up_now != app_net_last_link_up) {
				app_net_last_link_up = link_up_now;
				asynth_display_set_n(link_up_now);
				ui_dirty = true;
			}
		}

		if (app_net_indicator_pending) {
			app_net_indicator_pending = false;
			asynth_display_set_n(app_net_indicator_state);
			ui_dirty = true;
		}

		if (app_network_activity_service(now_ms)) {
			ui_dirty = true;
		}
#endif

		dropped = asynth_midi_take_drop_count();
		if (dropped != 0U) {
			printk("MIDI IN: dropped %u bytes (queue full)\n", (unsigned int)dropped);
		}

		asynth_midi_log_diag(&midi_diag_last_log_ms);

		if (process_trigger_events()) {
			ui_dirty = true;
		}

		if (asynth_display_tick_status_message_scroll()) {
			ui_dirty = true;
		}

		if (cue_pending_tick(now_ms)) {
			ui_dirty = true;
		}

		if (app_idle_tick(now_ms)) {
			ui_dirty = true;
		}

		if (asynth_display_tick_activity(now_ms)) {
			ui_dirty = true;
		}

		if (k_uptime_get() >= next_cv_sample_ms) {
			ret = asynth_cv_sample_and_process(cv_hysteresis_norm);
			if (ret == 0) {
				asynth_cv_refresh_bars();
			}

			//if (current_mode == APP_MODE_NORMAL && !cue_pending_active) {
			//	asynth_display_print_cue(current_cue_value);
			//}

			ui_dirty = true;
			next_cv_sample_ms = k_uptime_get() + cv_sample_period_ms;
		}

		if (ui_dirty) {
			cfb_framebuffer_finalize(oled);
		}
		k_sleep(K_MSEC(5));
	}
	return 0;
}
