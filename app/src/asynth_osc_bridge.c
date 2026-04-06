/*
 * Asynth OSC bridge (schema validation + transport hook).
 */

#include <app/asynth_osc_bridge.h>

#include <errno.h>
#include <zephyr/sys/util.h>

#define CV_CHANNEL_COUNT 4U

#define MIDI_CHANNEL_MAX 15U
#define MIDI_DATA_MAX 127U

static bool transport_enabled;
static struct asynth_osc_transport_ops transport_ops;

static int asynth_osc_validate_cv(float normalized)
{
	if (normalized < 0.0f || normalized > 1.0f) {
		return -ERANGE;
	}

	return 0;
}

static int asynth_osc_validate_trigger(bool active)
{
	if ((active != false) && (active != true)) {
		return -ERANGE;
	}

	return 0;
}

static int asynth_osc_validate_midi_channel(uint8_t channel)
{
	if (channel > MIDI_CHANNEL_MAX) {
		return -ERANGE;
	}

	return 0;
}

static int asynth_osc_validate_midi_data(uint8_t value)
{
	if (value > MIDI_DATA_MAX) {
		return -ERANGE;
	}

	return 0;
}

static int asynth_osc_stub_send(void)
{
	/* Transport intentionally disabled for now. */
	return 0;
}

void asynth_osc_set_transport_enabled(bool enabled)
{
	transport_enabled = enabled;
}

bool asynth_osc_is_transport_enabled(void)
{
	return transport_enabled;
}

void asynth_osc_set_transport_ops(const struct asynth_osc_transport_ops *ops)
{
	if (ops == NULL) {
		transport_ops.send_cv = NULL;
		transport_ops.send_trigger = NULL;
		transport_ops.send_midi_note_on = NULL;
		transport_ops.send_midi_note_off = NULL;
		transport_ops.send_midi_pc = NULL;
		transport_ops.send_midi_cc = NULL;
		return;
	}

	transport_ops.send_cv = ops->send_cv;
	transport_ops.send_trigger = ops->send_trigger;
	transport_ops.send_midi_note_on = ops->send_midi_note_on;
	transport_ops.send_midi_note_off = ops->send_midi_note_off;
	transport_ops.send_midi_pc = ops->send_midi_pc;
	transport_ops.send_midi_cc = ops->send_midi_cc;
}

int asynth_osc_send_cv(uint8_t cv_index, float normalized)
{
	int ret;

	if (cv_index >= CV_CHANNEL_COUNT) {
		return -EINVAL;
	}

	ret = asynth_osc_validate_cv(normalized);
	if (ret < 0) {
		return ret;
	}

	if (transport_enabled) {
		if (transport_ops.send_cv == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_cv(cv_index, normalized);
	}

	ARG_UNUSED(cv_index);
	ARG_UNUSED(normalized);
	return asynth_osc_stub_send();
}

int asynth_osc_send_trigger(uint8_t trigger_index, bool active)
{
	int ret;

	if (trigger_index > 1U) {
		return -EINVAL;
	}

	ret = asynth_osc_validate_trigger(active);
	if (ret < 0) {
		return ret;
	}

	if (transport_enabled) {
		if (transport_ops.send_trigger == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_trigger(trigger_index, active);
	}

	ARG_UNUSED(trigger_index);
	ARG_UNUSED(active);
	return asynth_osc_stub_send();
}

int asynth_osc_send_midi_note_on(uint8_t channel, uint8_t pitch, uint8_t velocity)
{
	int ret;

	ret = asynth_osc_validate_midi_channel(channel);
	if (ret < 0) {
		return ret;
	}

	ret = asynth_osc_validate_midi_data(pitch);
	if (ret < 0) {
		return ret;
	}

	ret = asynth_osc_validate_midi_data(velocity);
	if (ret < 0) {
		return ret;
	}

	if (transport_enabled) {
		if (transport_ops.send_midi_note_on == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_midi_note_on(channel, pitch, velocity);
	}

	ARG_UNUSED(channel);
	ARG_UNUSED(pitch);
	ARG_UNUSED(velocity);
	return asynth_osc_stub_send();
}

int asynth_osc_send_midi_note_off(uint8_t channel, uint8_t pitch)
{
	int ret;

	ret = asynth_osc_validate_midi_channel(channel);
	if (ret < 0) {
		return ret;
	}

	ret = asynth_osc_validate_midi_data(pitch);
	if (ret < 0) {
		return ret;
	}

	if (transport_enabled) {
		if (transport_ops.send_midi_note_off == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_midi_note_off(channel, pitch);
	}

	ARG_UNUSED(channel);
	ARG_UNUSED(pitch);
	return asynth_osc_stub_send();
}

int asynth_osc_send_midi_pc(uint8_t channel, uint8_t program)
{
	int ret;

	ret = asynth_osc_validate_midi_channel(channel);
	if (ret < 0) {
		return ret;
	}

	ret = asynth_osc_validate_midi_data(program);
	if (ret < 0) {
		return ret;
	}

	if (transport_enabled) {
		if (transport_ops.send_midi_pc == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_midi_pc(channel, program);
	}

	ARG_UNUSED(channel);
	ARG_UNUSED(program);
	return asynth_osc_stub_send();
}

int asynth_osc_send_midi_cc(uint8_t channel, uint8_t number, uint8_t value)
{
	int ret;

	ret = asynth_osc_validate_midi_channel(channel);
	if (ret < 0) {
		return ret;
	}

	ret = asynth_osc_validate_midi_data(number);
	if (ret < 0) {
		return ret;
	}

	ret = asynth_osc_validate_midi_data(value);
	if (ret < 0) {
		return ret;
	}

	if (transport_enabled) {
		if (transport_ops.send_midi_cc == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_midi_cc(channel, number, value);
	}

	ARG_UNUSED(channel);
	ARG_UNUSED(number);
	ARG_UNUSED(value);
	return asynth_osc_stub_send();
}
