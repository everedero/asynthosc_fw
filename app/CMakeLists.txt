# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cfb)

find_package(Git QUIET)

set(ASYNTH_BUILD_ID "b:unknown")
if(GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_SOURCE_DIR}/.. rev-parse --short=7 HEAD
		RESULT_VARIABLE GIT_HASH_RESULT
		OUTPUT_VARIABLE GIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	if(GIT_HASH_RESULT EQUAL 0 AND NOT "${GIT_HASH}" STREQUAL "")
		set(ASYNTH_BUILD_ID "b:${GIT_HASH}")

		execute_process(
			COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_SOURCE_DIR}/.. diff --quiet
			RESULT_VARIABLE GIT_DIRTY_RESULT
			OUTPUT_QUIET
			ERROR_QUIET
		)

		if(GIT_DIRTY_RESULT EQUAL 1)
			string(APPEND ASYNTH_BUILD_ID "*")
		endif()
	endif()
endif()

target_sources(app PRIVATE
	src/main.c
	src/asynth_cv.c
	src/asynth_midi.c
	src/asynth_osc_bridge.c
	src/asynth_display.c
	src/asynth_ui.c
)

target_compile_definitions(app PRIVATE ASYNTH_BUILD_ID="${ASYNTH_BUILD_ID}")

