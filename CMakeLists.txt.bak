# ac6recomp - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ac6recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(AC6RECOMP_SOURCES
    src/main.cpp
    src/render_hooks.cpp
    src/d3d_hooks.cpp
)

if(WIN32)
    add_executable(ac6recomp WIN32 ${AC6RECOMP_SOURCES})
else()
    add_executable(ac6recomp ${AC6RECOMP_SOURCES})
endif()

rexglue_setup_target(ac6recomp)

