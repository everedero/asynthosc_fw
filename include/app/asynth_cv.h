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

int asynth_cv_init(const struct device *display_dev);
int asynth_cv_sample_and_process(float hysteresis_norm);
void asynth_cv_refresh_bars(void);

#ifdef __cplusplus
}
#endif

#endif
