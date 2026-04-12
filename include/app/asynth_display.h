/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 ASynthOSC contributors */

/*
 * Asynth display helpers for OLED status, cue, and activity indicators.
 */

#ifndef APP_ASYNTH_DISPLAY_H_
#define APP_ASYNTH_DISPLAY_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

int asynth_display_init(const struct device *display_dev);

int asynth_display_print_msg(const char *msg);
int asynth_display_print_cue(uint32_t cue);
void asynth_display_clear_cue(void);

void asynth_display_act_t1(void);
void asynth_display_act_t2(void);
void asynth_display_act_m(void);
void asynth_display_act_a(void);
void asynth_display_act_n(void);
void asynth_display_set_a(bool on);
void asynth_display_set_n(bool on);

bool asynth_display_tick_status_message_scroll(void);
bool asynth_display_tick_activity(uint32_t now_ms);
void asynth_display_reset_activity(void);

#ifdef __cplusplus
}
#endif

#endif
