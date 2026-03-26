/*
 * Asynth MIDI input helpers.
 */

#ifndef APP_ASYNTH_MIDI_H_
#define APP_ASYNTH_MIDI_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int asynth_midi_init(void);
void asynth_midi_poll_fallback(void);
void asynth_midi_sample_uart_errors(void);
void asynth_midi_log_diag(uint32_t *last_log_ms);
bool asynth_midi_process_events(void);
uint32_t asynth_midi_take_drop_count(void);

#ifdef __cplusplus
}
#endif

#endif
