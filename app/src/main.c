/*
 * Asynth2OSC module main code
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <app/osc_schema.h>
#if defined(CONFIG_NETWORKING)
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_ip.h>
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
#define CV_BAR_MAX_PIXELS     28U
#define CV_ADC_RESOLUTION     12U
#define CV_ADC_MAX_RAW        ((1U << CV_ADC_RESOLUTION) - 1U)
#define CV_ADC_FULL_SCALE_MV  3300U

#define CV_SAMPLE_PERIOD_MIN_MS           20U
#define CV_SAMPLE_PERIOD_MAX_MS           200U
#define CV_SAMPLE_PERIOD_STEP_MS          5U
#define CV_SAMPLE_PERIOD_DEFAULT_MS       50U

#define CV_HYSTERESIS_MIN_PERMILLE        5U
#define CV_HYSTERESIS_MAX_PERMILLE        50U
#define CV_HYSTERESIS_STEP_PERMILLE       5U
#define CV_HYSTERESIS_DEFAULT_PERMILLE    10U

#define BUTTON_DEBOUNCE_MS    30U
#define ROTARY_LONG_PRESS_MS  2000U
#define ROTARY_DETENT_STEPS   4
#define CUE_PENDING_TIMEOUT_MS 5000U
#define CUE_PENDING_BLINK_INTERVAL_MS 300U

#define UI_STATUS_MSG_VISIBLE_CHARS      5U
#define UI_STATUS_MSG_MAX_LEN            64U
#define UI_STATUS_MSG_SCROLL_GAP         3U
#define UI_STATUS_MSG_SCROLL_INTERVAL_MS 180U

#define TRIGGER_BLINK_DURATION_MS        100U
#define TRIGGER_EVENT_LOG_ENABLE         1U
#define BUTTON_EVENT_LOG_ENABLE          0U

#define CV_CONFIG_NODE DT_PATH(zephyr_user)

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

#define NET_SETTINGS_DEFAULT_IP_MODE         0U
#define NET_SETTINGS_DEFAULT_TARGET_IP4      71U
#define NET_SETTINGS_DEFAULT_TARGET_PORT     1U
#define NET_SETTINGS_DEFAULT_DEVICE_IP4      72U
#define NET_SETTINGS_DEFAULT_DEVICE_PORT     2U
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

#if !DT_NODE_EXISTS(CV_CONFIG_NODE)
#error "Missing devicetree node /zephyr,user"
#endif

BUILD_ASSERT(DT_NODE_HAS_PROP(CV_CONFIG_NODE, cv_channel_ids),
	     "Missing /zephyr,user cv-channel-ids");
BUILD_ASSERT(DT_NODE_HAS_PROP(CV_CONFIG_NODE, cv_display_remap),
	     "Missing /zephyr,user cv-display-remap");
BUILD_ASSERT(DT_PROP_LEN(CV_CONFIG_NODE, cv_channel_ids) == CV_CHANNEL_COUNT,
	     "cv-channel-ids length must match CV_CHANNEL_COUNT");
BUILD_ASSERT(DT_PROP_LEN(CV_CONFIG_NODE, cv_display_remap) == CV_CHANNEL_COUNT,
	     "cv-display-remap length must match CV_CHANNEL_COUNT");

#define CV_CHANNEL_ID_FROM_DTS(idx, _) DT_PROP_BY_IDX(CV_CONFIG_NODE, cv_channel_ids, idx)
#define CV_DISPLAY_REMAP_FROM_DTS(idx, _) DT_PROP_BY_IDX(CV_CONFIG_NODE, cv_display_remap, idx)

static const uint8_t cv_adc_channel_ids[CV_CHANNEL_COUNT] = {
	LISTIFY(CV_CHANNEL_COUNT, CV_CHANNEL_ID_FROM_DTS, (,))
};
/* ADC index -> UI and OSC CV index remap. */
static const uint8_t cv_display_remap[CV_CHANNEL_COUNT] = {
	LISTIFY(CV_CHANNEL_COUNT, CV_DISPLAY_REMAP_FROM_DTS, (,))
};
static const struct device *cv_adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc1));

/* Global CV settings (to be replaced by menu/settings later). */
static uint32_t cv_sample_period_ms = CV_SAMPLE_PERIOD_DEFAULT_MS;
static uint16_t cv_hysteresis_permille = CV_HYSTERESIS_DEFAULT_PERMILLE;
static float cv_hysteresis_norm = 0.01f;
static float cv_adc_full_scale_volts = 3.3f;
static float cv_adc_calib_min_volts = 0.0f;
static float cv_adc_calib_max_volts = 3.3f;
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

static int16_t cv_raw_samples[CV_CHANNEL_COUNT];
static float cv_norm_values[CV_CHANNEL_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
static float cv_prev_sent_values[CV_CHANNEL_COUNT] = { -1.0f, -1.0f, -1.0f, -1.0f };

static struct adc_sequence cv_adc_sequence = {
	.buffer = cv_raw_samples,
	.buffer_size = sizeof(cv_raw_samples),
	.resolution = CV_ADC_RESOLUTION,
	.channels = 0U,
};

static struct gpio_callback left_button_cb_data;
static struct gpio_callback right_button_cb_data;
static struct gpio_callback rot_button_cb_data;
static struct gpio_callback rot_encoder_b_cb_data;

static volatile bool left_button_pressed;
static volatile bool left_button_changed;
static volatile uint32_t left_button_last_irq_ms;

static volatile bool right_button_pressed;
static volatile bool right_button_changed;
static volatile uint32_t right_button_last_irq_ms;

static volatile bool rot_button_pressed;
static volatile bool rot_button_changed;
static volatile uint32_t rot_button_last_irq_ms;
static volatile int8_t rot_encoder_delta;
static volatile uint8_t rot_encoder_prev_state;

static volatile bool trigger_1_active;
static volatile bool trigger_2_active;
static volatile uint32_t trigger_1_blink_end_ms;
static volatile uint32_t trigger_2_blink_end_ms;
static volatile bool midi_active;
static volatile bool audio_active;
static volatile uint32_t midi_blink_end_ms;
static volatile uint32_t audio_blink_end_ms;
static bool trigger_1_prev_state;
static bool trigger_2_prev_state;

static enum app_mode current_mode = APP_MODE_NORMAL;
static uint8_t current_menu_item = MENU_ITEM_SAMPLE_PERIOD;
static uint32_t rot_press_start_ms;
static bool rot_press_tracking;
static bool rot_button_was_pressed_last_poll;
static bool rot_button_debounced_pressed;
static bool rot_button_raw_pressed_prev;
static uint32_t rot_button_last_raw_change_ms;
static bool rot_button_state_initialized;
static int8_t rot_encoder_step_accum;

static char ui_status_msg[UI_STATUS_MSG_MAX_LEN + 1] = "";
static size_t ui_status_msg_len;
static size_t ui_status_msg_offset;
static int64_t ui_status_msg_next_scroll_ms;

#if defined(CONFIG_NETWORKING)
#define APP_NET_EVENT_MASK (NET_EVENT_IF_UP | NET_EVENT_IF_DOWN | \
			    NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED | \
			    NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL)

static struct net_mgmt_event_callback app_net_mgmt_cb;

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
		return;
	}

	if (mgmt_event == NET_EVENT_IF_DOWN) {
		printk("NET: interface down\n");
		return;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		printk("NET: L4 connected\n");
		app_print_ipv4_status(iface);
		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		printk("NET: L4 disconnected\n");
		return;
	}

	if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
		printk("NET: IPv4 address added\n");
		app_print_ipv4_status(iface);
		return;
	}

	if (mgmt_event == NET_EVENT_IPV4_ADDR_DEL) {
		printk("NET: IPv4 address removed\n");
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

static int net_settings_init(void)
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
	net_settings_ready = true;
	net_settings_log_current("startup");
	adc_settings_log_current("startup");

	return 0;
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

static float clampf(float value, float min_val, float max_val)
{
	if (value < min_val) {
		return min_val;
	}
	if (value > max_val) {
		return max_val;
	}
	return value;
}

static float cv_raw_to_voltage(int16_t raw)
{
	int32_t safe_raw = raw;

	if (safe_raw < 0) {
		safe_raw = 0;
	}
	if (safe_raw > (int32_t)CV_ADC_MAX_RAW) {
		safe_raw = (int32_t)CV_ADC_MAX_RAW;
	}

	return ((float)safe_raw / (float)CV_ADC_MAX_RAW) * cv_adc_full_scale_volts;
}

static float cv_voltage_to_normalized(float voltage)
{
	float span = cv_adc_calib_max_volts - cv_adc_calib_min_volts;

	if (span <= 0.0f) {
		return 0.0f;
	}

	return clampf((voltage - cv_adc_calib_min_volts) / span, 0.0f, 1.0f);
}

static uint8_t cv_normalized_to_pixels(float normalized)
{
	float clamped = clampf(normalized, 0.0f, 1.0f);
	float scaled = clamped * (float)CV_BAR_MAX_PIXELS;

	return (uint8_t)(scaled + 0.5f);
}

static int osc_out_send_cv(uint8_t cv_index, float normalized)
{
	osc_msg_t msg;
	const osc_msg_spec_t *spec;
	int ret;

	if (cv_index >= CV_CHANNEL_COUNT) {
		return -EINVAL;
	}

	msg.id = (osc_msg_id_t)(OSC_CV1 + cv_index);
	msg.value.f = normalized;

	spec = osc_get_spec(msg.id);
	if (!spec) {
		return -ENOENT;
	}
	ARG_UNUSED(spec);

	/*
	 * UART CV monitoring is intentionally disabled for now.
	 * Keep this quick debug snippet for future range checks:
	 * int32_t milli = (int32_t)(normalized * 1000.0f);
	 * printk("CV%d: %s = %d/1000\n", cv_index + 1, spec->path, milli);
	 */

	/* OSC transport is temporarily disabled while validating ADC/OLED path. */
	ARG_UNUSED(msg);
	ret = 0;

	return ret;
}

static int osc_out_send_trigger(uint8_t trigger_index, bool active)
{
	osc_msg_t msg;
	const osc_msg_spec_t *spec;
	osc_msg_id_t msg_id;
	int ret;

	if (trigger_index > 1U) {
		return -EINVAL;
	}

	msg_id = (trigger_index == 0U) ? OSC_TRIGGER1 : OSC_TRIGGER2;
	msg.id = msg_id;
	msg.value.i = active ? 1 : 0;

	spec = osc_get_spec(msg.id);
	if (!spec) {
		return -ENOENT;
	}

	if (!osc_check_direction(msg.id, OSC_DIR_OUTPUT)) {
		return -EPERM;
	}

	if (!osc_validate_value(msg.id, &msg.value)) {
		return -ERANGE;
	}

	/* OSC transport is intentionally disabled for now. */
	ARG_UNUSED(spec);
	ARG_UNUSED(msg);
	ret = 0;

	return ret;
}

static int cv_validate_remap(const uint8_t *remap, const char *name)
{
	uint32_t seen = 0U;

	for (int i = 0; i < CV_CHANNEL_COUNT; i++) {
		if (remap[i] >= CV_CHANNEL_COUNT) {
			printk("Invalid CV %s remap index %d -> %u\n",
			       name, i, (unsigned int)remap[i]);
			return -EINVAL;
		}

		if ((seen & BIT(remap[i])) != 0U) {
			printk("Duplicate CV %s remap value at index %d -> %u\n",
			       name, i, (unsigned int)remap[i]);
			return -EINVAL;
		}

		seen |= BIT(remap[i]);
	}

	return 0;
}

static int cv_adc_init(void)
{
	struct adc_channel_cfg channel_cfg = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
	};
	int ret;

	ret = cv_validate_remap(cv_display_remap, "display");
	if (ret < 0) {
		return ret;
	}

	if (!device_is_ready(cv_adc_dev)) {
		printk("ADC device %s is not ready\n", cv_adc_dev->name);
		return -ENODEV;
	}

	for (int i = 0; i < CV_CHANNEL_COUNT; i++) {
		channel_cfg.channel_id = cv_adc_channel_ids[i];
#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
		channel_cfg.input_positive = cv_adc_channel_ids[i];
#endif
		ret = adc_channel_setup(cv_adc_dev, &channel_cfg);
		if (ret < 0) {
			printk("Failed to setup ADC channel %d (%d)\n", cv_adc_channel_ids[i], ret);
			return ret;
		}
		cv_adc_sequence.channels |= BIT(cv_adc_channel_ids[i]);
	}

	return 0;
}

static int cv_sample_and_process(void)
{
	int ret = adc_read(cv_adc_dev, &cv_adc_sequence);

	if (ret < 0) {
		printk("ADC read failed (%d)\n", ret);
		return ret;
	}

	for (int i = 0; i < CV_CHANNEL_COUNT; i++) {
		float voltage = cv_raw_to_voltage(cv_raw_samples[i]);
		float normalized = cv_voltage_to_normalized(voltage);
		float delta;

		cv_norm_values[i] = normalized;

		delta = normalized - cv_prev_sent_values[i];
		if (delta < 0.0f) {
			delta = -delta;
		}

		if (cv_prev_sent_values[i] < 0.0f || delta >= cv_hysteresis_norm) {
			cv_prev_sent_values[i] = normalized;
			uint8_t cv_osc_idx = cv_display_remap[i];
			ret = osc_out_send_cv(cv_osc_idx, normalized);
			if (ret < 0) {
				printk("OSC send CV%d failed (%d)\n", cv_osc_idx + 1, ret);
			}
		}
	}

	return 0;
}

static void update_cv_bars(uint8_t cv_pixels[CV_CHANNEL_COUNT])
{
	for (int adc_idx = 0; adc_idx < CV_CHANNEL_COUNT; adc_idx++) {
		uint8_t display_pos = cv_display_remap[adc_idx]; /* Which display position for this ADC index */
		uint8_t new_pixels = cv_normalized_to_pixels(cv_norm_values[adc_idx]);

		if (new_pixels == cv_pixels[display_pos]) {
			continue;
		}

		/* Erase old level and draw new level using the existing invert workflow. */
		cfb_invert_area(oled, 82 + display_pos * 12, 18, 7, 29 - cv_pixels[display_pos]);
		cv_pixels[display_pos] = new_pixels;
		cfb_invert_area(oled, 82 + display_pos * 12, 18, 7, 29 - cv_pixels[display_pos]);
	}
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
/**
 * @brief Render the status line message area (always 5 characters wide).
 *
 * @return 0
 */
static int printMsg_render_window(void)
{
	char window[UI_STATUS_MSG_VISIBLE_CHARS + 1];
	size_t cycle_len;
	size_t i;

	memset(window, ' ', UI_STATUS_MSG_VISIBLE_CHARS);

	if (ui_status_msg_len <= UI_STATUS_MSG_VISIBLE_CHARS) {
		memcpy(window, ui_status_msg, ui_status_msg_len);
	} else {
		cycle_len = ui_status_msg_len + UI_STATUS_MSG_SCROLL_GAP;
		for (i = 0U; i < UI_STATUS_MSG_VISIBLE_CHARS; i++) {
			size_t idx = (ui_status_msg_offset + i) % cycle_len;

			if (idx < ui_status_msg_len) {
				window[i] = ui_status_msg[idx];
			}
		}
	}

	window[UI_STATUS_MSG_VISIBLE_CHARS] = '\0';
	cfb_framebuffer_set_font(oled, 1);
	cfb_set_kerning(oled, 0);
	cfb_print(oled, window, 0, 28);
	return 0;
}

/**
 * @brief Print a status message in a 5-char area with marquee scrolling for long strings.
 * oled device is set as global parameter
 *
 * @param msg: message to display (NULL-safe)
 * @return 0
 */
int printMsg(const char *msg)
{
	char bounded_msg[UI_STATUS_MSG_MAX_LEN + 1];
	size_t len = 0U;
	bool changed;

	if (msg == NULL) {
		msg = "";
	}

	while ((len < UI_STATUS_MSG_MAX_LEN) && (msg[len] != '\0')) {
		len++;
	}

	memcpy(bounded_msg, msg, len);
	bounded_msg[len] = '\0';

	changed = (strcmp(ui_status_msg, bounded_msg) != 0);
	if (changed) {
		memcpy(ui_status_msg, bounded_msg, len + 1U);
		ui_status_msg_len = len;
		ui_status_msg_offset = 0U;
		ui_status_msg_next_scroll_ms = k_uptime_get() + UI_STATUS_MSG_SCROLL_INTERVAL_MS;
	}

	return printMsg_render_window();
}

static bool ui_tick_status_message_scroll(void)
{
	int64_t now_ms;
	size_t cycle_len;

	if (ui_status_msg_len <= UI_STATUS_MSG_VISIBLE_CHARS) {
		return false;
	}

	now_ms = k_uptime_get();
	if (now_ms < ui_status_msg_next_scroll_ms) {
		return false;
	}

	cycle_len = ui_status_msg_len + UI_STATUS_MSG_SCROLL_GAP;
	ui_status_msg_offset = (ui_status_msg_offset + 1U) % cycle_len;
	ui_status_msg_next_scroll_ms = now_ms + UI_STATUS_MSG_SCROLL_INTERVAL_MS;
	printMsg_render_window();
	return true;
}

/**
 * @brief Print Cue number on a reserved place on oled screen (always 3 digits: 000..999).
 * oled device is set as global parameter
 *
 * @param cue: numeric cue value
 * @return 0
 */
int printCue(uint32_t cue)
{
	char buffer[4];
	uint32_t safe_cue = cue % 1000U;

	snprintf(buffer, sizeof(buffer), "%03u", (unsigned int)safe_cue);
	cfb_framebuffer_set_font(oled, 2);
	cfb_set_kerning(oled, 0);
	cfb_print(oled, buffer, 0, 0);
	return 0;
}

/**
 * @brief Blink T1 trigger 1 input activity led on oled screen
 * 
 */
void actT1()
{
	if (!oled || !device_is_ready(oled)) {
		return;
	}
	cfb_invert_area(oled, 80, 0, 11, 14);
	trigger_1_active = true;
	trigger_1_blink_end_ms = k_uptime_get_32() + TRIGGER_BLINK_DURATION_MS;
}
/**
 * @brief Blink M MIDI input activity LED on OLED screen.
 *
 * Reserved for future implementation when MIDI input parsing is wired.
 *
 */
void actM()
{
	if (!oled || !device_is_ready(oled)) {
		return;
	}
	cfb_invert_area(oled, 92, 0, 11, 14);
	midi_active = true;
	midi_blink_end_ms = k_uptime_get_32() + TRIGGER_BLINK_DURATION_MS;
}
/**
 * @brief Blink A audio activity LED on OLED screen.
 *
 * Reserved for future implementation when DAC audio generation is wired.
 *
 */
void actA()
{
	if (!oled || !device_is_ready(oled)) {
		return;
	}
	cfb_invert_area(oled, 104, 0, 11, 14);
	audio_active = true;
	audio_blink_end_ms = k_uptime_get_32() + TRIGGER_BLINK_DURATION_MS;
}
/**
 * @brief Blink T2 trigger 2 input activity led on oled screen
 *
 */
void actT2()
{
	if (!oled || !device_is_ready(oled)) {
		return;
	}
	cfb_invert_area(oled, 116, 0, 11, 14);
	trigger_2_active = true;
	trigger_2_blink_end_ms = k_uptime_get_32() + TRIGGER_BLINK_DURATION_MS;
}

static bool gpio_level_is_pressed(const struct gpio_dt_spec *button, int gpio_level)
{
	bool active_low = (button->dt_flags & GPIO_ACTIVE_LOW) != 0U;

	if (active_low) {
		return gpio_level == 0;
	}

	return gpio_level != 0;
}

static int gpio_pin_get_raw_dt(const struct gpio_dt_spec *spec)
{
	return gpio_pin_get_raw(spec->port, spec->pin);
}

static void button_log_state(const char *name,
			     const struct gpio_dt_spec *button,
			     int gpio_level,
			     bool pressed,
			     const char *source)
{
#if BUTTON_EVENT_LOG_ENABLE
	bool active_low = (button->dt_flags & GPIO_ACTIVE_LOW) != 0U;

	BUTTON_EVENT_LOG("BTN %-5s %-4s raw=%d active_%s => %s\n",
			 name,
			 source,
			 gpio_level,
			 active_low ? "LOW" : "HIGH",
			 pressed ? "PRESSED" : "RELEASED");
#else
	ARG_UNUSED(name);
	ARG_UNUSED(button);
	ARG_UNUSED(gpio_level);
	ARG_UNUSED(pressed);
	ARG_UNUSED(source);
#endif
}

static void button_irq_update(const char *name,
			      const struct gpio_dt_spec *button,
			      volatile bool *pressed_state,
			      volatile bool *changed_flag,
			      volatile uint32_t *last_irq_ms)
{
	uint32_t now_ms = k_uptime_get_32();
	int gpio_level;
	bool new_pressed_state;

	if ((uint32_t)(now_ms - *last_irq_ms) < BUTTON_DEBOUNCE_MS) {
		return;
	}

	gpio_level = gpio_pin_get_raw_dt(button);
	if (gpio_level < 0) {
		return;
	}

	new_pressed_state = gpio_level_is_pressed(button, gpio_level);
	if (new_pressed_state != *pressed_state) {
		*pressed_state = new_pressed_state;
		*changed_flag = true;
		*last_irq_ms = now_ms;
		button_log_state(name, button, gpio_level, new_pressed_state, "IRQ");
	}
}

static void left_button_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	button_irq_update("LEFT", &left_button, &left_button_pressed, &left_button_changed,
			  &left_button_last_irq_ms);
}

static void right_button_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	button_irq_update("RIGHT", &right_button, &right_button_pressed, &right_button_changed,
			  &right_button_last_irq_ms);
}

static void rot_button_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	button_irq_update("ROT", &rot_button, &rot_button_pressed, &rot_button_changed,
			  &rot_button_last_irq_ms);
}

static int rotary_encoder_read_state(uint8_t *state_out)
{
	int level_a;
	int level_b;
	uint8_t state = 0U;

	level_a = gpio_pin_get_raw_dt(&rot_encoder_a);
	if (level_a < 0) {
		return level_a;
	}

	level_b = gpio_pin_get_raw_dt(&rot_encoder_b);
	if (level_b < 0) {
		return level_b;
	}

	if (gpio_level_is_pressed(&rot_encoder_a, level_a)) {
		state |= 0x2U;
	}
	if (gpio_level_is_pressed(&rot_encoder_b, level_b)) {
		state |= 0x1U;
	}

	*state_out = state;
	return 0;
}

static void rotary_encoder_irq_update(void)
{
	unsigned int key;
	uint8_t old_state;
	uint8_t old_b;
	uint8_t new_b;
	uint8_t a;
	uint8_t new_state;
	int16_t accum;
	int ret;

	ret = rotary_encoder_read_state(&new_state);
	if (ret < 0) {
		return;
	}

	key = irq_lock();
	old_state = rot_encoder_prev_state;
	old_b = old_state & 0x1U;
	new_b = new_state & 0x1U;

	if (new_b != old_b) {
		a = (new_state >> 1) & 0x1U;
		accum = (int16_t)rot_encoder_delta + ((a == new_b) ? -1 : 1);
		if (accum > 32) {
			accum = 32;
		} else if (accum < -32) {
			accum = -32;
		}
		rot_encoder_delta = (int8_t)accum;
	}
	rot_encoder_prev_state = new_state;
	irq_unlock(key);
}

static void rot_encoder_b_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	rotary_encoder_irq_update();
}

static int configure_button_interrupt(const struct gpio_dt_spec *button,
			      struct gpio_callback *cb_data,
			      gpio_callback_handler_t cb_handler)
{
	int ret;

	if (!device_is_ready(button->port)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(button, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(button, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(cb_data, cb_handler, BIT(button->pin));
	ret = gpio_add_callback(button->port, cb_data);

	return ret;
}

static int buttons_init(void)
{
	int ret;
	int level;
	uint8_t encoder_state = 0U;

	ret = configure_button_interrupt(&left_button, &left_button_cb_data, left_button_cb);
	if (ret < 0) {
		return ret;
	}

	ret = configure_button_interrupt(&right_button, &right_button_cb_data, right_button_cb);
	if (ret < 0) {
		return ret;
	}

	ret = configure_button_interrupt(&rot_button, &rot_button_cb_data, rot_button_cb);
	if (ret < 0) {
		return ret;
	}

	if (!device_is_ready(rot_encoder_a.port) || !device_is_ready(rot_encoder_b.port)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&rot_encoder_a, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&rot_encoder_b, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	/* Keep encoder IRQ on B (PD14) and poll triggers to avoid EXTI line conflicts. */
	ret = gpio_pin_interrupt_configure_dt(&rot_encoder_b, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&rot_encoder_b_cb_data, rot_encoder_b_cb, BIT(rot_encoder_b.pin));
	ret = gpio_add_callback(rot_encoder_b.port, &rot_encoder_b_cb_data);
	if (ret < 0) {
		return ret;
	}

	level = gpio_pin_get_raw_dt(&left_button);
	if (level >= 0) {
		left_button_pressed = gpio_level_is_pressed(&left_button, level);
		button_log_state("LEFT", &left_button, level, left_button_pressed, "INIT");
	}

	level = gpio_pin_get_raw_dt(&right_button);
	if (level >= 0) {
		right_button_pressed = gpio_level_is_pressed(&right_button, level);
		button_log_state("RIGHT", &right_button, level, right_button_pressed, "INIT");
	}

	level = gpio_pin_get_raw_dt(&rot_button);
	rot_button_state_initialized = false;
	rot_button_debounced_pressed = false;
	rot_button_raw_pressed_prev = false;
	rot_button_last_raw_change_ms = k_uptime_get_32();
	rot_button_was_pressed_last_poll = false;
	rot_press_tracking = false;
	if (level >= 0) {
		bool initial_rot_pressed = gpio_level_is_pressed(&rot_button, level);

		rot_button_pressed = initial_rot_pressed;
		rot_button_state_initialized = true;
		rot_button_debounced_pressed = initial_rot_pressed;
		rot_button_raw_pressed_prev = initial_rot_pressed;
		rot_button_was_pressed_last_poll = initial_rot_pressed;
		button_log_state("ROT", &rot_button, level, initial_rot_pressed, "INIT");
	}

	if (rotary_encoder_read_state(&encoder_state) == 0) {
		rot_encoder_prev_state = encoder_state;
	}
	rot_encoder_delta = 0;
	rot_encoder_step_accum = 0;

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

	level = gpio_pin_get_raw_dt(&trigger_1);
	if (level < 0) {
		return level;
	}
	trigger_1_prev_state = gpio_level_is_pressed(&trigger_1, level);

	level = gpio_pin_get_raw_dt(&trigger_2);
	if (level < 0) {
		return level;
	}
	trigger_2_prev_state = gpio_level_is_pressed(&trigger_2, level);

	trigger_1_active = false;
	trigger_2_active = false;
	trigger_1_blink_end_ms = 0U;
	trigger_2_blink_end_ms = 0U;
	midi_active = false;
	audio_active = false;
	midi_blink_end_ms = 0U;
	audio_blink_end_ms = 0U;

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
			printMsg("SR ms");
			printCue(cv_sample_period_ms);
			break;
		case MENU_ITEM_HYSTERESIS:
			printMsg("HY mV");
			value = (uint16_t)(((uint32_t)cv_hysteresis_permille * CV_ADC_FULL_SCALE_MV + 500U) /
					   1000U);
			printCue(value);
			break;
		case MENU_ITEM_NET_IP_MODE:
			printMsg("IPMd ");
			printCue(net_cfg.ip_mode);
			break;
		case MENU_ITEM_NET_TARGET_IP4:
			printMsg("T IP ");
			printCue(net_cfg.target_ip4);
			break;
		case MENU_ITEM_NET_TARGET_PORT:
			printMsg("TPort");
			printCue(net_cfg.target_port);
			break;
		case MENU_ITEM_NET_DEVICE_IP4:
			printMsg("D IP ");
			printCue(net_cfg.device_ip4);
			break;
		case MENU_ITEM_NET_DEVICE_PORT:
			printMsg("DPort");
			printCue(net_cfg.device_port);
			break;
		case MENU_ITEM_NET_CIDR_MASK:
			printMsg("CIDR ");
			printCue(net_cfg.cidr_mask);
			break;
		case MENU_ITEM_NET_IP_B1:
			printMsg("IPb1 ");
			printCue(net_cfg.ip_b1);
			break;
		case MENU_ITEM_NET_IP_B2:
			printMsg("IPb2 ");
			printCue(net_cfg.ip_b2);
			break;
		case MENU_ITEM_NET_IP_B3:
			printMsg("IPb3 ");
			printCue(net_cfg.ip_b3);
			break;
		default:
		// Should not happen, but clear the area if it does.
			printMsg("     ");
			printCue(0);
			break;
		}
	} else {
		// In normal mode, the status line is reserved for transient messages, so clear it when writing other info to avoid confusion.
		printMsg("     ");
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
	printCue(current_cue_value);
	ui_apply_button_visual_state();
}

static void cue_send_recall(uint16_t cue_value)
{
	char msg_buf[32];

	snprintf(msg_buf, sizeof(msg_buf), "Recall Cue #%u", (unsigned int)cue_value);
	printMsg(msg_buf);

	/* Dedicated hook for future cue-recall side effects (OSC, etc.). */
}

static void cue_print_blank(void)
{
	cfb_framebuffer_set_font(oled, 2);
	cfb_set_kerning(oled, 0);
	cfb_print(oled, "   ", 0, 0);
}

static void printTempCueBlink(uint16_t cue_value, bool visible)
{
	if (visible) {
		printCue(cue_value);
	} else {
		cue_print_blank();
	}
}

static void cue_pending_cancel(bool show_status)
{
	if (!cue_pending_active) {
		return;
	}

	cue_pending_active = false;
	cue_pending_visible = true;
	printCue(current_cue_value);

	if (show_status) {
		printMsg("Cue cancel");
	}
}

static void cue_pending_commit_or_recall(void)
{
	if (cue_pending_active) {
		current_cue_value = cue_pending_value;
		cue_pending_active = false;
		cue_pending_visible = true;
		printCue(current_cue_value);
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

	printCue(current_cue_value);
}

static int8_t pop_rotary_delta(void)
{
	unsigned int key;
	int8_t delta;

	key = irq_lock();
	delta = rot_encoder_delta;
	rot_encoder_delta = 0;
	irq_unlock(key);

	return delta;
}

static bool pop_button_event(volatile bool *changed_flag,
			     volatile bool *pressed_state,
			     bool *pressed_state_out)
{
	unsigned int key;
	bool changed;

	key = irq_lock();
	changed = *changed_flag;
	if (changed) {
		*changed_flag = false;
		*pressed_state_out = *pressed_state;
	}
	irq_unlock(key);

	return changed;
}

static bool rot_button_get_debounced_state(uint32_t now_ms, bool *pressed_out)
{
	int level;
	bool raw_pressed;

	level = gpio_pin_get_raw_dt(&rot_button);
	if (level < 0) {
		*pressed_out = rot_button_debounced_pressed;
		return false;
	}

	raw_pressed = gpio_level_is_pressed(&rot_button, level);

	if (!rot_button_state_initialized) {
		rot_button_state_initialized = true;
		rot_button_raw_pressed_prev = raw_pressed;
		rot_button_debounced_pressed = raw_pressed;
		rot_button_last_raw_change_ms = now_ms;
	} else {
		if (raw_pressed != rot_button_raw_pressed_prev) {
			rot_button_raw_pressed_prev = raw_pressed;
			rot_button_last_raw_change_ms = now_ms;
		}

		if ((rot_button_debounced_pressed != rot_button_raw_pressed_prev) &&
		    ((uint32_t)(now_ms - rot_button_last_raw_change_ms) >= BUTTON_DEBOUNCE_MS)) {
			rot_button_debounced_pressed = rot_button_raw_pressed_prev;
			button_log_state("ROT", &rot_button, level,
					 rot_button_debounced_pressed, "DEB");
		}
	}

	*pressed_out = rot_button_debounced_pressed;
	return true;
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

	if (pop_button_event(&left_button_changed, &left_button_pressed, &left_pressed_now)) {
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

	if (pop_button_event(&right_button_changed, &right_button_pressed, &right_pressed_now)) {
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
	(void)rot_button_get_debounced_state(now_ms, &rot_button_state_now);

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
		(void)pop_rotary_delta();
		rot_encoder_step_accum = 0;
		return ui_dirty;
	}

	delta = pop_rotary_delta();
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

	level = gpio_pin_get_raw_dt(&trigger_1);
	if (level >= 0) {
		state = gpio_level_is_pressed(&trigger_1, level);
		if (state != trigger_1_prev_state) {
			trigger_1_prev_state = state;
			actT1();
			ret = osc_out_send_trigger(0U, state);
			if (ret < 0) {
				printk("OSC send Trigger1 failed (%d)\n", ret);
			}
			TRIGGER_EVENT_LOG("Trigger 1 %s\n", state ? "ON" : "OFF");
			ui_dirty = true;
		}
	}

	level = gpio_pin_get_raw_dt(&trigger_2);
	if (level >= 0) {
		state = gpio_level_is_pressed(&trigger_2, level);
		if (state != trigger_2_prev_state) {
			trigger_2_prev_state = state;
			actT2();
			ret = osc_out_send_trigger(1U, state);
			if (ret < 0) {
				printk("OSC send Trigger2 failed (%d)\n", ret);
			}
			TRIGGER_EVENT_LOG("Trigger 2 %s\n", state ? "ON" : "OFF");
			ui_dirty = true;
		}
	}

	return ui_dirty;
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
	uint8_t CV[CV_CHANNEL_COUNT] = { 0, 0, 0, 0 };
	int64_t next_cv_sample_ms;

	// Initialize top left right buttons leds
	init_led(left_led);
	init_led(right_led);
	// turn them off
	ret = gpio_pin_toggle_dt(&right_led);
	if (ret < 0) {
		return 0;
	}
	ret = gpio_pin_toggle_dt(&left_led);
	if (ret < 0) {
		return 0;
	}

	ret = buttons_init();
	if (ret < 0) {
		printk("Buttons init failed (%d)\n", ret);
		return 0;
	}

	ret = net_settings_init();
	if (ret < 0) {
		printk("NETCFG: using defaults (%d)\n", ret);
	}

	// Intialize Oled screen
	oled = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(oled)) {
		return 0;
	}

	if (display_set_pixel_format(oled, PIXEL_FORMAT_MONO10) != 0) {
		if (display_set_pixel_format(oled, PIXEL_FORMAT_MONO01) != 0) {
			return 0;
		}
	}

	if (cfb_framebuffer_init(oled)) {
		return 0;
	}

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
	k_sleep(K_MSEC(2000));
	cfb_framebuffer_clear(oled, true);
	cfb_framebuffer_finalize(oled);

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
	cfb_print(oled, "1MA2", 80, 0);

	// Init center mode hint and button visual states
	ui_write_center_mode_hint();
	ui_write_status_line();
	printCue(current_cue_value);
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

	ret = cv_adc_init();
	if (ret < 0) {
		return 0;
	}

#if defined(CONFIG_NETWORKING)
	ret = app_network_init();
	if (ret < 0) {
		printk("NET: initialization failed (%d)\n", ret);
	}
#else
	printk("NET: networking is disabled in this build\n");
#endif

	next_cv_sample_ms = k_uptime_get();

	/* 
	*	Main infinite loop
	*/

	while (1) {
		bool ui_dirty = process_button_events();
		uint32_t now_ms = k_uptime_get_32();

		if (process_trigger_events()) {
			ui_dirty = true;
		}

		if (ui_tick_status_message_scroll()) {
			ui_dirty = true;
		}

		if (cue_pending_tick(now_ms)) {
			ui_dirty = true;
		}

		if (trigger_1_active && (now_ms >= trigger_1_blink_end_ms)) {
			cfb_invert_area(oled, 80, 0, 11, 14);
			trigger_1_active = false;
			ui_dirty = true;
		}

		if (trigger_2_active && (now_ms >= trigger_2_blink_end_ms)) {
			cfb_invert_area(oled, 116, 0, 11, 14);
			trigger_2_active = false;
			ui_dirty = true;
		}

		if (midi_active && (now_ms >= midi_blink_end_ms)) {
			cfb_invert_area(oled, 92, 0, 11, 14);
			midi_active = false;
			ui_dirty = true;
		}

		if (audio_active && (now_ms >= audio_blink_end_ms)) {
			cfb_invert_area(oled, 104, 0, 11, 14);
			audio_active = false;
			ui_dirty = true;
		}

		if (k_uptime_get() >= next_cv_sample_ms) {
			ret = cv_sample_and_process();
			if (ret == 0) {
				update_cv_bars(CV);
			}

			if (current_mode == APP_MODE_NORMAL && !cue_pending_active) {
				printCue(current_cue_value);
			}

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
