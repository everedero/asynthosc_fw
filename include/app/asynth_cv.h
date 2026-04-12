/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 ASynthOSC contributors */

/*
 * Asynth CV/ADC helpers.
 */

#ifndef APP_ASYNTH_CV_H_
#define APP_ASYNTH_CV_H_

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASYNTH_CV_CHANNEL_COUNT 4

int asynth_cv_init(const struct device *display_dev);
int asynth_cv_sample_and_process(float hysteresis_norm);
void asynth_cv_refresh_bars(void);
void asynth_cv_reset_display_cache(void);

#ifdef __cplusplus
}
#endif

#endif
