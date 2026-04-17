# Copyright (c) Pavel Kirienko

cmake_minimum_required(VERSION 3.13)

# Default to Pico 2 unless the user overrides it.
if (NOT PICO_BOARD)
  set(PICO_BOARD pico2)
endif()

include(pico_sdk_import.cmake)

project(o1heap_perftest C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(o1heap_perftest
  main.c
  ${CMAKE_CURRENT_LIST_DIR}/../o1heap/o1heap.c
)

pico_set_binary_type(o1heap_perftest copy_to_ram)

target_include_directories(o1heap_perftest PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../o1heap
)

target_compile_definitions(o1heap_perftest PRIVATE
  PICO_STDIO_UART=1
  PICO_STDIO_USB=0
)

target_compile_options(o1heap_perftest PRIVATE
  -O3
  -flto
)

target_link_options(o1heap_perftest PRIVATE
  -flto
  -Wl,-u,__wrap_printf
  -Wl,-u,__wrap_vprintf
  -Wl,-u,__wrap_puts
  -Wl,-u,__wrap_putchar
  -Wl,-u,__wrap_getchar
)

target_link_libraries(o1heap_perftest
  pico_stdlib
  hardware_clocks
  hardware_sync
)

pico_enable_stdio_uart(o1heap_perftest 1)
pico_enable_stdio_usb(o1heap_perftest 0)

pico_add_extra_outputs(o1heap_perftest)
