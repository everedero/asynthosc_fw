# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# Out-of-tree drivers for custom classes
add_subdirectory_ifdef(CONFIG_BLINK blink)
add_subdirectory_ifdef(CONFIG_DAC_AUDIO_STM32 audio_dac)

# Out-of-tree drivers for existing driver classes
add_subdirectory_ifdef(CONFIG_SENSOR sensor)
