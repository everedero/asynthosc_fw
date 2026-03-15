/*
 * OSC Schema - Auto-generated from doc/OSC_dictionnary.csv
 * 
 * Defines OSC message paths, types, and validation helpers.
 */

#ifndef APP_OSC_SCHEMA_H_
#define APP_OSC_SCHEMA_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OSC Message IDs - indexed by CSV row order */
typedef enum {
	OSC_TRIGGER1 = 0,
	OSC_TRIGGER2,
	OSC_CV1,
	OSC_CV2,
	OSC_CV3,
	OSC_CV4,
	OSC_MIDI_NOTE_ON,
	OSC_MIDI_NOTE_OFF,
	OSC_MIDI_PC,
	OSC_MIDI_CC,
	OSC_PING,
	OSC_PONG,
	OSC_MSG,
	OSC_MSG_COUNT
} osc_msg_id_t;

/* OSC Message Direction */
typedef enum {
	OSC_DIR_UNKNOWN = 0,
	OSC_DIR_INPUT = 1,   /* 'i' - device receives */
	OSC_DIR_OUTPUT = 2,  /* 'o' - device sends */
} osc_dir_t;

/* OSC Message Metadata */
typedef struct {
	osc_msg_id_t id;
	const char *path;
	const char *type_tag;  /* OSC type tags: "i", "f", "s", "iii", etc. */
	osc_dir_t direction;
	const char *description;
	float min_val;
	float max_val;
} osc_msg_spec_t;

/* OSC Message Value Union */
typedef union {
	int32_t i;
	float f;
	const char *s;
	struct {
		int32_t v0, v1, v2;
	} iii;
	struct {
		int32_t v0, v1;
	} ii;
} osc_value_t;

/* OSC Message Packet for Queue */
typedef struct {
	osc_msg_id_t id;
	osc_value_t value;
} osc_msg_t;

/**
 * @brief Get OSC message specification by ID
 * @param id Message ID
 * @return Pointer to message spec, or NULL if invalid
 */
const osc_msg_spec_t *osc_get_spec(osc_msg_id_t id);

/**
 * @brief Lookup OSC message ID by path string
 * @param path OSC path (e.g., "/asynth/cv1")
 * @param id Output parameter for message ID
 * @return true if found, false otherwise
 */
bool osc_lookup_path(const char *path, osc_msg_id_t *id);

/**
 * @brief Validate OSC message value against schema constraints
 * @param id Message ID
 * @param value Value to validate
 * @return true if valid, false otherwise
 */
bool osc_validate_value(osc_msg_id_t id, const osc_value_t *value);

/**
 * @brief Check if message direction matches expected
 * @param id Message ID
 * @param expected_dir Expected direction (INPUT or OUTPUT)
 * @return true if matches, false otherwise
 */
bool osc_check_direction(osc_msg_id_t id, osc_dir_t expected_dir);

#ifdef __cplusplus
}
#endif

#endif /* APP_OSC_SCHEMA_H_ */
