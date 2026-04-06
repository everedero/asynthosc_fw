/*
 * Asynth display helpers for OLED status, cue, and activity indicators.
 */

#include <app/asynth_display.h>

#include <zephyr/display/cfb.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define UI_STATUS_MSG_VISIBLE_CHARS      8U
#define UI_STATUS_MSG_MAX_LEN            64U
#define UI_STATUS_MSG_SCROLL_GAP         3U
#define UI_STATUS_MSG_SCROLL_INTERVAL_MS 180U

#define UI_STATUS_MSG_FONT_IDX           0U
#define UI_STATUS_MSG_X                  0U
#define UI_STATUS_MSG_Y                  33U

#define TRIGGER_BLINK_DURATION_MS        100U

static const struct device *oled;

static bool trigger_1_active;
static bool trigger_2_active;
static bool midi_active;
static bool audio_active;

static uint32_t trigger_1_blink_end_ms;
static uint32_t trigger_2_blink_end_ms;
static uint32_t midi_blink_end_ms;
static uint32_t audio_blink_end_ms;

static char ui_status_msg[UI_STATUS_MSG_MAX_LEN + 1] = "";
static size_t ui_status_msg_len;
static size_t ui_status_msg_offset;
static int64_t ui_status_msg_next_scroll_ms;

static bool asynth_display_ready(void)
{
	return (oled != NULL) && device_is_ready(oled);
}

static int asynth_display_render_msg_window(void)
{
	char window[UI_STATUS_MSG_VISIBLE_CHARS + 1];
	size_t cycle_len;
	size_t i;

	if (!asynth_display_ready()) {
		return -ENODEV;
	}

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
	cfb_framebuffer_set_font(oled, UI_STATUS_MSG_FONT_IDX);
	cfb_set_kerning(oled, 0);
	cfb_print(oled, window, UI_STATUS_MSG_X, UI_STATUS_MSG_Y);
	return 0;
}

int asynth_display_init(const struct device *display_dev)
{
	oled = display_dev;
	ui_status_msg[0] = '\0';
	ui_status_msg_len = 0U;
	ui_status_msg_offset = 0U;
	ui_status_msg_next_scroll_ms = 0;
	asynth_display_reset_activity();

	if (!asynth_display_ready()) {
		return -ENODEV;
	}

	return 0;
}

int asynth_display_print_msg(const char *msg)
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

	return asynth_display_render_msg_window();
}

bool asynth_display_tick_status_message_scroll(void)
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
	asynth_display_render_msg_window();
	return true;
}

int asynth_display_print_cue(uint32_t cue)
{
	char buffer[4];
	uint32_t safe_cue = cue % 1000U;

	if (!asynth_display_ready()) {
		return -ENODEV;
	}

	snprintf(buffer, sizeof(buffer), "%03u", (unsigned int)safe_cue);
	cfb_framebuffer_set_font(oled, 2);
	cfb_set_kerning(oled, 0);
	cfb_print(oled, buffer, 0, 0);
	return 0;
}

void asynth_display_clear_cue(void)
{
	if (!asynth_display_ready()) {
		return;
	}

	cfb_framebuffer_set_font(oled, 2);
	cfb_set_kerning(oled, 0);
	cfb_print(oled, "   ", 0, 0);
}

void asynth_display_act_t1(void)
{
	if (!asynth_display_ready()) {
		return;
	}

	cfb_invert_area(oled, 80, 0, 11, 14);
	trigger_1_active = true;
	trigger_1_blink_end_ms = k_uptime_get_32() + TRIGGER_BLINK_DURATION_MS;
}

void asynth_display_act_t2(void)
{
	if (!asynth_display_ready()) {
		return;
	}

	cfb_invert_area(oled, 116, 0, 11, 14);
	trigger_2_active = true;
	trigger_2_blink_end_ms = k_uptime_get_32() + TRIGGER_BLINK_DURATION_MS;
}

void asynth_display_act_m(void)
{
	if (!asynth_display_ready()) {
		return;
	}

	cfb_invert_area(oled, 92, 0, 11, 14);
	midi_active = true;
	midi_blink_end_ms = k_uptime_get_32() + TRIGGER_BLINK_DURATION_MS;
}

void asynth_display_act_a(void)
{
	if (!asynth_display_ready()) {
		return;
	}

	cfb_invert_area(oled, 104, 0, 11, 14);
	audio_active = true;
	audio_blink_end_ms = k_uptime_get_32() + TRIGGER_BLINK_DURATION_MS;
}

bool asynth_display_tick_activity(uint32_t now_ms)
{
	bool ui_dirty = false;

	if (!asynth_display_ready()) {
		return false;
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

	return ui_dirty;
}

void asynth_display_reset_activity(void)
{
	trigger_1_active = false;
	trigger_2_active = false;
	midi_active = false;
	audio_active = false;
	trigger_1_blink_end_ms = 0U;
	trigger_2_blink_end_ms = 0U;
	midi_blink_end_ms = 0U;
	audio_blink_end_ms = 0U;
}
