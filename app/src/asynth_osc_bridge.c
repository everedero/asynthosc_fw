/*
 * Asynth OSC bridge (schema validation + transport hook).
 */

#include <app/asynth_osc_bridge.h>

#include <errno.h>
#include <zephyr/sys/util.h>

#define CV_CHANNEL_COUNT 4U

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
		return;
	}

	transport_ops.send_cv = ops->send_cv;
	transport_ops.send_trigger = ops->send_trigger;
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
