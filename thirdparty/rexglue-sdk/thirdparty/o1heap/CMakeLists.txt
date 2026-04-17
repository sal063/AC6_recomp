# This software is distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT
# Copyright (c) Pavel Kirienko
# Authors: Pavel Kirienko <pavel.kirienko@zubax.com>
#
# This file is only needed for library development and testing. It is not needed to use the library in your project;
# instead, users should integrate the library by copying o1heap.c and o1heap.h.

cmake_minimum_required(VERSION 3.20)

project(o1heap)
enable_testing()
set(CMAKE_CTEST_ARGUMENTS "-V")  # Enable test outputs when running `make test`
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)
