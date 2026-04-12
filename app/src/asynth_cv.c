/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 ASynthOSC contributors */

/*
 * Asynth CV/ADC helpers.
 */

#include <app/asynth_cv.h>

#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/util.h>
#include <app/asynth_osc_bridge.h>

#define CV_CHANNEL_COUNT      4
#define CV_BAR_MAX_PIXELS     28U
#define CV_ADC_RESOLUTION     12U
#define CV_ADC_MAX_RAW        ((1U << CV_ADC_RESOLUTION) - 1U)

#define CV_CONFIG_NODE DT_PATH(zephyr_user)

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
static const struct device *oled;

static int16_t cv_raw_samples[CV_CHANNEL_COUNT];
static float cv_norm_values[CV_CHANNEL_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
static float cv_prev_sent_values[CV_CHANNEL_COUNT] = { -1.0f, -1.0f, -1.0f, -1.0f };
static uint8_t cv_pixels[CV_CHANNEL_COUNT] = { 0, 0, 0, 0 };

static struct adc_sequence cv_adc_sequence = {
	.buffer = cv_raw_samples,
	.buffer_size = sizeof(cv_raw_samples),
	.resolution = CV_ADC_RESOLUTION,
	.channels = 0U,
};

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

	return ((float)safe_raw / (float)CV_ADC_MAX_RAW) * 3.3f;
}

static float cv_voltage_to_normalized(float voltage)
{
	return clampf(voltage / 3.3f, 0.0f, 1.0f);
}

static uint8_t cv_normalized_to_pixels(float normalized)
{
	float clamped = clampf(normalized, 0.0f, 1.0f);
	float scaled = clamped * (float)CV_BAR_MAX_PIXELS;

	return (uint8_t)(scaled + 0.5f);
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

int asynth_cv_init(const struct device *display_dev)
{
	struct adc_channel_cfg channel_cfg = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
	};
	int ret;

	oled = display_dev;

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

int asynth_cv_sample_and_process(float hysteresis_norm)
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

		if (cv_prev_sent_values[i] < 0.0f || delta >= hysteresis_norm) {
			cv_prev_sent_values[i] = normalized;
			uint8_t cv_osc_idx = cv_display_remap[i];
			ret = asynth_osc_send_cv(cv_osc_idx, normalized);
			if (ret < 0) {
				printk("OSC send CV%d failed (%d)\n", cv_osc_idx + 1, ret);
			}
		}
	}

	return 0;
}

void asynth_cv_refresh_bars(void)
{
	if (!oled || !device_is_ready(oled)) {
		return;
	}

	for (int adc_idx = 0; adc_idx < CV_CHANNEL_COUNT; adc_idx++) {
		uint8_t display_pos = cv_display_remap[adc_idx];
		uint8_t new_pixels = cv_normalized_to_pixels(cv_norm_values[adc_idx]);

		if (new_pixels == cv_pixels[display_pos]) {
			continue;
		}

		cfb_invert_area(oled, 82 + display_pos * 12, 18, 7, 29 - cv_pixels[display_pos]);
		cv_pixels[display_pos] = new_pixels;
		cfb_invert_area(oled, 82 + display_pos * 12, 18, 7, 29 - cv_pixels[display_pos]);
	}
}
