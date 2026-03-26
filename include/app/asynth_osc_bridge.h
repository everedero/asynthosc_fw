/*
 * Asynth OSC bridge (schema validation + transport hook).
 */

#ifndef APP_ASYNTH_OSC_BRIDGE_H_
#define APP_ASYNTH_OSC_BRIDGE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*asynth_osc_send_cv_cb_t)(uint8_t cv_index, float normalized);
typedef int (*asynth_osc_send_trigger_cb_t)(uint8_t trigger_index, bool active);

struct asynth_osc_transport_ops {
	asynth_osc_send_cv_cb_t send_cv;
	asynth_osc_send_trigger_cb_t send_trigger;
};

void asynth_osc_set_transport_enabled(bool enabled);
bool asynth_osc_is_transport_enabled(void);
void asynth_osc_set_transport_ops(const struct asynth_osc_transport_ops *ops);

int asynth_osc_send_cv(uint8_t cv_index, float normalized);
int asynth_osc_send_trigger(uint8_t trigger_index, bool active);

#ifdef __cplusplus
}
#endif

#endif
