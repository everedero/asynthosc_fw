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
typedef int (*asynth_osc_send_midi_note_on_cb_t)(uint8_t channel, uint8_t pitch, uint8_t velocity);
typedef int (*asynth_osc_send_midi_note_off_cb_t)(uint8_t channel, uint8_t pitch);
typedef int (*asynth_osc_send_midi_pc_cb_t)(uint8_t channel, uint8_t program);
typedef int (*asynth_osc_send_midi_cc_cb_t)(uint8_t channel, uint8_t number, uint8_t value);
typedef int (*asynth_osc_send_midi_pitch_bend_cb_t)(uint8_t channel, int16_t value);
typedef int (*asynth_osc_send_midi_mmc_cb_t)(uint8_t dev_id, uint8_t command);
typedef int (*asynth_osc_send_midi_mtc_qf_cb_t)(uint8_t piece, uint8_t value);
typedef int (*asynth_osc_send_midi_mtc_ff_cb_t)(uint32_t packed);

struct asynth_osc_transport_ops {
	asynth_osc_send_cv_cb_t send_cv;
	asynth_osc_send_trigger_cb_t send_trigger;
	asynth_osc_send_midi_note_on_cb_t send_midi_note_on;
	asynth_osc_send_midi_note_off_cb_t send_midi_note_off;
	asynth_osc_send_midi_pc_cb_t send_midi_pc;
	asynth_osc_send_midi_cc_cb_t send_midi_cc;
	asynth_osc_send_midi_pitch_bend_cb_t send_midi_pitch_bend;
	asynth_osc_send_midi_mmc_cb_t send_midi_mmc;
	asynth_osc_send_midi_mtc_qf_cb_t send_midi_mtc_qf;
	asynth_osc_send_midi_mtc_ff_cb_t send_midi_mtc_ff;
};

void asynth_osc_set_transport_enabled(bool enabled);
bool asynth_osc_is_transport_enabled(void);
void asynth_osc_set_transport_ops(const struct asynth_osc_transport_ops *ops);

int asynth_osc_send_cv(uint8_t cv_index, float normalized);
int asynth_osc_send_trigger(uint8_t trigger_index, bool active);
int asynth_osc_send_midi_note_on(uint8_t channel, uint8_t pitch, uint8_t velocity);
int asynth_osc_send_midi_note_off(uint8_t channel, uint8_t pitch);
int asynth_osc_send_midi_pc(uint8_t channel, uint8_t program);
int asynth_osc_send_midi_cc(uint8_t channel, uint8_t number, uint8_t value);
int asynth_osc_send_midi_pitch_bend(uint8_t channel, int16_t value);
int asynth_osc_send_midi_mmc(uint8_t dev_id, uint8_t command);
int asynth_osc_send_midi_mtc_qf(uint8_t piece, uint8_t value);
int asynth_osc_send_midi_mtc_ff(uint32_t packed);

#ifdef __cplusplus
}
#endif

#endif
