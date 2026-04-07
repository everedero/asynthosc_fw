/*
 * Asynth UI helpers (buttons, encoder).
 */

#include <app/asynth_ui.h>

#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <errno.h>

static const struct gpio_dt_spec *s_left_button;
static const struct gpio_dt_spec *s_right_button;
static const struct gpio_dt_spec *s_rot_button;
static const struct gpio_dt_spec *s_rot_encoder_a;
static const struct gpio_dt_spec *s_rot_encoder_b;

static struct gpio_callback s_left_button_cb_data;
static struct gpio_callback s_right_button_cb_data;
static struct gpio_callback s_rot_button_cb_data;
static struct gpio_callback s_rot_encoder_b_cb_data;

static volatile bool s_left_button_pressed;
static volatile bool s_left_button_changed;
static volatile uint32_t s_left_button_last_irq_ms;

static volatile bool s_right_button_pressed;
static volatile bool s_right_button_changed;
static volatile uint32_t s_right_button_last_irq_ms;

static volatile bool s_rot_button_pressed;
static volatile bool s_rot_button_changed;
static volatile uint32_t s_rot_button_last_irq_ms;

static volatile int8_t s_rot_encoder_delta;
static volatile uint8_t s_rot_encoder_prev_state;

static bool s_rot_button_debounced_pressed;
static bool s_rot_button_raw_pressed_prev;
static uint32_t s_rot_button_last_raw_change_ms;
static bool s_rot_button_state_initialized;

static uint32_t s_button_debounce_ms;

bool asynth_ui_level_is_pressed(const struct gpio_dt_spec *button, int gpio_level)
{
	bool active_low = (button->dt_flags & GPIO_ACTIVE_LOW) != 0U;

	if (active_low) {
		return gpio_level == 0;
	}

	return gpio_level != 0;
}

int asynth_ui_pin_get_raw_dt(const struct gpio_dt_spec *spec)
{
	return gpio_pin_get_raw(spec->port, spec->pin);
}

static void button_irq_update(const struct gpio_dt_spec *button,
			      volatile bool *pressed_state,
			      volatile bool *changed_flag,
			      volatile uint32_t *last_irq_ms)
{
	uint32_t now_ms = k_uptime_get_32();
	int gpio_level;
	bool new_pressed_state;

	if ((uint32_t)(now_ms - *last_irq_ms) < s_button_debounce_ms) {
		return;
	}

	gpio_level = asynth_ui_pin_get_raw_dt(button);
	if (gpio_level < 0) {
		return;
	}

	new_pressed_state = asynth_ui_level_is_pressed(button, gpio_level);
	if (new_pressed_state != *pressed_state) {
		*pressed_state = new_pressed_state;
		*changed_flag = true;
		*last_irq_ms = now_ms;
	}
}

static void left_button_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	button_irq_update(s_left_button, &s_left_button_pressed, &s_left_button_changed,
			  &s_left_button_last_irq_ms);
}

static void right_button_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	button_irq_update(s_right_button, &s_right_button_pressed, &s_right_button_changed,
			  &s_right_button_last_irq_ms);
}

static void rot_button_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	button_irq_update(s_rot_button, &s_rot_button_pressed, &s_rot_button_changed,
			  &s_rot_button_last_irq_ms);
}

static int rotary_encoder_read_state(uint8_t *state_out)
{
	int level_a;
	int level_b;
	uint8_t state = 0U;

	level_a = asynth_ui_pin_get_raw_dt(s_rot_encoder_a);
	if (level_a < 0) {
		return level_a;
	}

	level_b = asynth_ui_pin_get_raw_dt(s_rot_encoder_b);
	if (level_b < 0) {
		return level_b;
	}

	if (asynth_ui_level_is_pressed(s_rot_encoder_a, level_a)) {
		state |= 0x2U;
	}
	if (asynth_ui_level_is_pressed(s_rot_encoder_b, level_b)) {
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
	old_state = s_rot_encoder_prev_state;
	old_b = old_state & 0x1U;
	new_b = new_state & 0x1U;

	if (new_b != old_b) {
		a = (new_state >> 1) & 0x1U;
		accum = (int16_t)s_rot_encoder_delta + ((a == new_b) ? -1 : 1);
		if (accum > 32) {
			accum = 32;
		} else if (accum < -32) {
			accum = -32;
		}
		s_rot_encoder_delta = (int8_t)accum;
	}
	
	s_rot_encoder_prev_state = new_state;
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

int asynth_ui_input_init(const struct asynth_ui_input_pins *pins, uint32_t debounce_ms)
{
	int ret;
	int level;
	uint8_t encoder_state = 0U;

	if (!pins || !pins->left_button || !pins->right_button || !pins->rot_button ||
	    !pins->rot_encoder_a || !pins->rot_encoder_b) {
		return -EINVAL;
	}

	s_left_button = pins->left_button;
	s_right_button = pins->right_button;
	s_rot_button = pins->rot_button;
	s_rot_encoder_a = pins->rot_encoder_a;
	s_rot_encoder_b = pins->rot_encoder_b;
	s_button_debounce_ms = debounce_ms;

	ret = configure_button_interrupt(s_left_button, &s_left_button_cb_data, left_button_cb);
	if (ret < 0) {
		return ret;
	}

	ret = configure_button_interrupt(s_right_button, &s_right_button_cb_data, right_button_cb);
	if (ret < 0) {
		return ret;
	}

	ret = configure_button_interrupt(s_rot_button, &s_rot_button_cb_data, rot_button_cb);
	if (ret < 0) {
		return ret;
	}

	if (!device_is_ready(s_rot_encoder_a->port) || !device_is_ready(s_rot_encoder_b->port)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(s_rot_encoder_a, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(s_rot_encoder_b, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(s_rot_encoder_b, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&s_rot_encoder_b_cb_data, rot_encoder_b_cb, BIT(s_rot_encoder_b->pin));
	ret = gpio_add_callback(s_rot_encoder_b->port, &s_rot_encoder_b_cb_data);
	if (ret < 0) {
		return ret;
	}

	level = asynth_ui_pin_get_raw_dt(s_left_button);
	if (level >= 0) {
		s_left_button_pressed = asynth_ui_level_is_pressed(s_left_button, level);
	}

	level = asynth_ui_pin_get_raw_dt(s_right_button);
	if (level >= 0) {
		s_right_button_pressed = asynth_ui_level_is_pressed(s_right_button, level);
	}

	level = asynth_ui_pin_get_raw_dt(s_rot_button);
	s_rot_button_state_initialized = false;
	s_rot_button_debounced_pressed = false;
	s_rot_button_raw_pressed_prev = false;
	s_rot_button_last_raw_change_ms = k_uptime_get_32();
	if (level >= 0) {
		bool initial_rot_pressed = asynth_ui_level_is_pressed(s_rot_button, level);

		s_rot_button_pressed = initial_rot_pressed;
		s_rot_button_state_initialized = true;
		s_rot_button_debounced_pressed = initial_rot_pressed;
		s_rot_button_raw_pressed_prev = initial_rot_pressed;
	}

	if (rotary_encoder_read_state(&encoder_state) == 0) {
		s_rot_encoder_prev_state = encoder_state;
	}
	s_rot_encoder_delta = 0;

	return 0;
}

bool asynth_ui_input_pop_button_event(enum asynth_ui_button_id button, bool *pressed_state_out)
{
	volatile bool *changed_flag;
	volatile bool *pressed_state;
	unsigned int key;
	bool changed;

	if (!pressed_state_out) {
		return false;
	}

	switch (button) {
	case ASYNTH_UI_BUTTON_LEFT:
		changed_flag = &s_left_button_changed;
		pressed_state = &s_left_button_pressed;
		break;
	case ASYNTH_UI_BUTTON_RIGHT:
		changed_flag = &s_right_button_changed;
		pressed_state = &s_right_button_pressed;
		break;
	case ASYNTH_UI_BUTTON_ROT:
		changed_flag = &s_rot_button_changed;
		pressed_state = &s_rot_button_pressed;
		break;
	default:
		return false;
	}

	key = irq_lock();
	changed = *changed_flag;
	if (changed) {
		*changed_flag = false;
		*pressed_state_out = *pressed_state;
	}
	irq_unlock(key);

	return changed;
}

int8_t asynth_ui_input_pop_rotary_delta(void)
{
	unsigned int key;
	int8_t delta;

	key = irq_lock();
	delta = s_rot_encoder_delta;
	s_rot_encoder_delta = 0;
	irq_unlock(key);

	return delta;
}

bool asynth_ui_input_get_rot_button_debounced(uint32_t now_ms, bool *pressed_out)
{
	int level;
	bool raw_pressed;

	if (!pressed_out) {
		return false;
	}

	level = asynth_ui_pin_get_raw_dt(s_rot_button);
	if (level < 0) {
		*pressed_out = s_rot_button_debounced_pressed;
		return false;
	}

	raw_pressed = asynth_ui_level_is_pressed(s_rot_button, level);

	if (!s_rot_button_state_initialized) {
		s_rot_button_state_initialized = true;
		s_rot_button_raw_pressed_prev = raw_pressed;
		s_rot_button_debounced_pressed = raw_pressed;
		s_rot_button_last_raw_change_ms = now_ms;
	} else {
		if (raw_pressed != s_rot_button_raw_pressed_prev) {
			s_rot_button_raw_pressed_prev = raw_pressed;
			s_rot_button_last_raw_change_ms = now_ms;
		}

		if ((s_rot_button_debounced_pressed != s_rot_button_raw_pressed_prev) &&
		    ((uint32_t)(now_ms - s_rot_button_last_raw_change_ms) >= s_button_debounce_ms)) {
			s_rot_button_debounced_pressed = s_rot_button_raw_pressed_prev;
		}
	}

	*pressed_out = s_rot_button_debounced_pressed;
	return true;
}
