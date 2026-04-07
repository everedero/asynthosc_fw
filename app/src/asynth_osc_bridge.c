/*
 * Asynth OSC bridge (schema validation + transport hook).
 */

#include <app/asynth_osc_bridge.h>

#include <errno.h>
#include <zephyr/sys/util.h>

#define CV_CHANNEL_COUNT 4U

#define MIDI_CHANNEL_MAX       15U
#define MIDI_DATA_MAX          127U
#define MIDI_PITCH_BEND_MIN    (-8192)
#define MIDI_PITCH_BEND_MAX    (8191)
#define MIDI_MTC_PIECE_MAX     7U
#define MIDI_MTC_VALUE_MAX     15U

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
		transport_ops.send_midi_pitch_bend = NULL;
		transport_ops.send_midi_mmc = NULL;
		transport_ops.send_midi_mtc_qf = NULL;
		transport_ops.send_midi_mtc_ff = NULL;
		return;
	}

	transport_ops.send_cv = ops->send_cv;
	transport_ops.send_trigger = ops->send_trigger;
	transport_ops.send_midi_note_on = ops->send_midi_note_on;
	transport_ops.send_midi_note_off = ops->send_midi_note_off;
	transport_ops.send_midi_pc = ops->send_midi_pc;
	transport_ops.send_midi_cc = ops->send_midi_cc;
	transport_ops.send_midi_pitch_bend = ops->send_midi_pitch_bend;
	transport_ops.send_midi_mmc = ops->send_midi_mmc;
	transport_ops.send_midi_mtc_qf = ops->send_midi_mtc_qf;
	transport_ops.send_midi_mtc_ff = ops->send_midi_mtc_ff;
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

int asynth_osc_send_midi_pitch_bend(uint8_t channel, int16_t value)
{
	int ret;

	ret = asynth_osc_validate_midi_channel(channel);
	if (ret < 0) {
		return ret;
	}

	if (value < MIDI_PITCH_BEND_MIN || value > MIDI_PITCH_BEND_MAX) {
		return -ERANGE;
	}

	if (transport_enabled) {
		if (transport_ops.send_midi_pitch_bend == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_midi_pitch_bend(channel, value);
	}

	ARG_UNUSED(channel);
	ARG_UNUSED(value);
	return asynth_osc_stub_send();
}

int asynth_osc_send_midi_mmc(uint8_t dev_id, uint8_t command)
{
	if (transport_enabled) {
		if (transport_ops.send_midi_mmc == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_midi_mmc(dev_id, command);
	}

	ARG_UNUSED(dev_id);
	ARG_UNUSED(command);
	return asynth_osc_stub_send();
}

int asynth_osc_send_midi_mtc_qf(uint8_t piece, uint8_t value)
{
	if (piece > MIDI_MTC_PIECE_MAX) {
		return -EINVAL;
	}

	if (value > MIDI_MTC_VALUE_MAX) {
		return -EINVAL;
	}

	if (transport_enabled) {
		if (transport_ops.send_midi_mtc_qf == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_midi_mtc_qf(piece, value);
	}

	ARG_UNUSED(piece);
	ARG_UNUSED(value);
	return asynth_osc_stub_send();
}

int asynth_osc_send_midi_mtc_ff(uint32_t packed)
{
	if (transport_enabled) {
		if (transport_ops.send_midi_mtc_ff == NULL) {
			return -ENOSYS;
		}

		return transport_ops.send_midi_mtc_ff(packed);
	}

	ARG_UNUSED(packed);
	return asynth_osc_stub_send();
}
