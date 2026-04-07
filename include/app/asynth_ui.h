#ifndef ASYNTH_UI_H_
#define ASYNTH_UI_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/gpio.h>

enum asynth_ui_button_id {
	ASYNTH_UI_BUTTON_LEFT = 0,
	ASYNTH_UI_BUTTON_RIGHT,
	ASYNTH_UI_BUTTON_ROT,
};

struct asynth_ui_input_pins {
	const struct gpio_dt_spec *left_button;
	const struct gpio_dt_spec *right_button;
	const struct gpio_dt_spec *rot_button;
	const struct gpio_dt_spec *rot_encoder_a;
	const struct gpio_dt_spec *rot_encoder_b;
};

int asynth_ui_input_init(const struct asynth_ui_input_pins *pins, uint32_t debounce_ms);
bool asynth_ui_input_pop_button_event(enum asynth_ui_button_id button, bool *pressed_state_out);
int8_t asynth_ui_input_pop_rotary_delta(void);
bool asynth_ui_input_get_rot_button_debounced(uint32_t now_ms, bool *pressed_out);

bool asynth_ui_level_is_pressed(const struct gpio_dt_spec *button, int gpio_level);
int asynth_ui_pin_get_raw_dt(const struct gpio_dt_spec *spec);

#endif /* ASYNTH_UI_H_ */
