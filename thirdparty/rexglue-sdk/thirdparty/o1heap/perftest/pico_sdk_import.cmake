# SPDX-License-Identifier: MIT
# Minimal Pico SDK import helper.

if (NOT PICO_SDK_PATH)
  if (DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
  else()
    message(FATAL_ERROR "PICO_SDK_PATH is not set; export it or pass -DPICO_SDK_PATH=...")
  endif()
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
