/*
 * OSC Schema Implementation
 * 
 * Data derived from doc/OSC_dictionnary.csv
 */

#include <app/osc_schema.h>
#include <string.h>

/* Static OSC Message Specifications - derived from CSV */
static const osc_msg_spec_t osc_specs[OSC_MSG_COUNT] = {
	/* OSC_TRIGGER1 */
	{
		.id = OSC_TRIGGER1,
		.path = "/asynth/trigger1",
		.type_tag = "i",  /* boolean as int 0/1 */
		.direction = OSC_DIR_OUTPUT,
		.description = "",
		.min_val = 0.0f,
		.max_val = 1.0f,
	},
	/* OSC_TRIGGER2 */
	{
		.id = OSC_TRIGGER2,
		.path = "/asynth/trigger2",
		.type_tag = "i",
		.direction = OSC_DIR_OUTPUT,
		.description = "",
		.min_val = 0.0f,
		.max_val = 1.0f,
	},
	/* OSC_CV1 */
	{
		.id = OSC_CV1,
		.path = "/asynth/cv1",
		.type_tag = "f",
		.direction = OSC_DIR_OUTPUT,
		.description = "CV 1 value",
		.min_val = 0.0f,
		.max_val = 1.0f,
	},
	/* OSC_CV2 */
	{
		.id = OSC_CV2,
		.path = "/asynth/cv2",
		.type_tag = "f",
		.direction = OSC_DIR_OUTPUT,
		.description = "CV 2 value",
		.min_val = 0.0f,
		.max_val = 1.0f,
	},
	/* OSC_CV3 */
	{
		.id = OSC_CV3,
		.path = "/asynth/cv3",
		.type_tag = "f",
		.direction = OSC_DIR_OUTPUT,
		.description = "CV 3 value",
		.min_val = 0.0f,
		.max_val = 1.0f,
	},
	/* OSC_CV4 */
	{
		.id = OSC_CV4,
		.path = "/asynth/cv4",
		.type_tag = "f",
		.direction = OSC_DIR_OUTPUT,
		.description = "CV 4 value",
		.min_val = 0.0f,
		.max_val = 1.0f,
	},
	/* OSC_MIDI_NOTE_ON */
	{
		.id = OSC_MIDI_NOTE_ON,
		.path = "/asynth/midi/noteON",
		.type_tag = "iii",
		.direction = OSC_DIR_OUTPUT,
		.description = "note on: channel, pitch, velocity",
		.min_val = 0.0f,
		.max_val = 127.0f,
	},
	/* OSC_MIDI_NOTE_OFF */
	{
		.id = OSC_MIDI_NOTE_OFF,
		.path = "/asynth/midi/noteOFF",
		.type_tag = "ii",
		.direction = OSC_DIR_OUTPUT,
		.description = "note off: channel, pitch",
		.min_val = 0.0f,
		.max_val = 127.0f,
	},
	/* OSC_MIDI_PC */
	{
		.id = OSC_MIDI_PC,
		.path = "/asynth/midi/PC",
		.type_tag = "ii",
		.direction = OSC_DIR_OUTPUT,
		.description = "program change: channel, program",
		.min_val = 0.0f,
		.max_val = 127.0f,
	},
	/* OSC_MIDI_CC */
	{
		.id = OSC_MIDI_CC,
		.path = "/asynth/midi/CC",
		.type_tag = "iii",
		.direction = OSC_DIR_OUTPUT,
		.description = "control change: channel, number, value",
		.min_val = 0.0f,
		.max_val = 127.0f,
	},
	/* OSC_PING */
	{
		.id = OSC_PING,
		.path = "/asynth/ping",
		.type_tag = "",
		.direction = OSC_DIR_INPUT,
		.description = "OSC input",
		.min_val = 0.0f,
		.max_val = 0.0f,
	},
	/* OSC_PONG */
	{
		.id = OSC_PONG,
		.path = "/asynth/pong",
		.type_tag = "",
		.direction = OSC_DIR_OUTPUT,
		.description = "",
		.min_val = 0.0f,
		.max_val = 0.0f,
	},
	/* OSC_MSG */
	{
		.id = OSC_MSG,
		.path = "/asynth/msg",
		.type_tag = "s",
		.direction = OSC_DIR_INPUT,
		.description = "OSC input",
		.min_val = 0.0f,
		.max_val = 0.0f,
	},
};

const osc_msg_spec_t *osc_get_spec(osc_msg_id_t id)
{
	if (id >= OSC_MSG_COUNT) {
		return NULL;
	}
	return &osc_specs[id];
}

bool osc_lookup_path(const char *path, osc_msg_id_t *id)
{
	if (!path || !id) {
		return false;
	}

	for (int i = 0; i < OSC_MSG_COUNT; i++) {
		if (strcmp(path, osc_specs[i].path) == 0) {
			*id = osc_specs[i].id;
			return true;
		}
	}
	return false;
}

bool osc_validate_value(osc_msg_id_t id, const osc_value_t *value)
{
	const osc_msg_spec_t *spec = osc_get_spec(id);
	if (!spec || !value) {
		return false;
	}

	/* Type-specific validation */
	if (strcmp(spec->type_tag, "f") == 0) {
		/* Float validation */
		return (value->f >= spec->min_val && value->f <= spec->max_val);
	} else if (strcmp(spec->type_tag, "i") == 0) {
		/* Integer validation (including boolean) */
		return (value->i >= (int32_t)spec->min_val && 
		        value->i <= (int32_t)spec->max_val);
	} else if (strcmp(spec->type_tag, "iii") == 0 || 
	           strcmp(spec->type_tag, "ii") == 0) {
		/* MIDI message validation - check all values in range */
		int32_t min = (int32_t)spec->min_val;
		int32_t max = (int32_t)spec->max_val;
		if (strcmp(spec->type_tag, "iii") == 0) {
			return (value->iii.v0 >= min && value->iii.v0 <= max &&
			        value->iii.v1 >= min && value->iii.v1 <= max &&
			        value->iii.v2 >= min && value->iii.v2 <= max);
		} else {
			return (value->ii.v0 >= min && value->ii.v0 <= max &&
			        value->ii.v1 >= min && value->ii.v1 <= max);
		}
	} else if (strcmp(spec->type_tag, "s") == 0) {
		/* String validation - just check non-NULL */
		return (value->s != NULL);
	}

	/* Unknown type or empty type tag */
	return true;
}

bool osc_check_direction(osc_msg_id_t id, osc_dir_t expected_dir)
{
	const osc_msg_spec_t *spec = osc_get_spec(id);
	if (!spec) {
		return false;
	}
	return (spec->direction == expected_dir);
}
