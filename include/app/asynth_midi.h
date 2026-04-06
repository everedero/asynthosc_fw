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

/* MIDI message types */
enum asynth_midi_msg_type {
	ASYNTH_MIDI_MSG_NOTE_OFF = 0x80,
	ASYNTH_MIDI_MSG_NOTE_ON = 0x90,
	ASYNTH_MIDI_MSG_KEY_PRESSURE = 0xA0,
	ASYNTH_MIDI_MSG_CONTROL_CHANGE = 0xB0,
	ASYNTH_MIDI_MSG_PROGRAM_CHANGE = 0xC0,
	ASYNTH_MIDI_MSG_CHANPRESSURE = 0xD0,
	ASYNTH_MIDI_MSG_PITCH_BEND = 0xE0,
	ASYNTH_MIDI_MSG_SYSTEM = 0xF0,
};

/* MIDI message structure */
struct asynth_midi_msg {
	uint8_t status;           /* Status byte (includes message type and channel) */
	uint8_t channel;          /* MIDI channel (0-15) */
	uint8_t data1;            /* First data byte */
	uint8_t data2;            /* Second data byte */
	enum asynth_midi_msg_type msg_type;
};

typedef void (*asynth_midi_handler_t)(const struct asynth_midi_msg *msg);

int asynth_midi_init(void);
void asynth_midi_poll_fallback(void);
void asynth_midi_sample_uart_errors(void);
void asynth_midi_log_diag(uint32_t *last_log_ms);
bool asynth_midi_process_events(void);
uint32_t asynth_midi_take_drop_count(void);

/* Handler registration */
void asynth_midi_set_note_handler(asynth_midi_handler_t handler);
void asynth_midi_set_control_change_handler(asynth_midi_handler_t handler);
void asynth_midi_set_program_change_handler(asynth_midi_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif
