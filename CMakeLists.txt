# ac6recomp - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ac6recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT AC6RECOMP_IPO_SUPPORTED OUTPUT AC6RECOMP_IPO_ERROR)

# Keep reconfigure passes pinned to the vendored SDK when the cache is empty.
if(NOT DEFINED REXSDK_DIR OR REXSDK_DIR STREQUAL "")
    set(_ac6_vendored_rexsdk "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk")
    if(EXISTS "${_ac6_vendored_rexsdk}/CMakeLists.txt")
        set(REXSDK_DIR "${_ac6_vendored_rexsdk}" CACHE PATH "Path to rexglue-sdk source tree" FORCE)
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake")
    include(generated/rexglue.cmake)
else()
    include(cmake/rexglue_bootstrap.cmake)
endif()

# Sources
set(AC6RECOMP_SOURCES
    src/main.cpp
    src/ac6_pac_decode_dump.cpp
    src/ac6_pac_decoder_probe.cpp
    src/ac6_pac_index.cpp
    src/d3d_hooks.cpp
    src/render_hooks.cpp
    src/ac6_texture_overrides.cpp
    src/ac6_native_graphics.cpp
    src/ac6_native_graphics_overlay.cpp
    src/ac6_backend_fixes/ac6_backend_capture_bridge.cpp
    src/ac6_backend_fixes/ac6_backend_hooks.cpp
    src/ac6_backend_fixes/ac6_backend_pass_classifier.cpp
)

if(WIN32)
    add_executable(ac6recomp WIN32 ${AC6RECOMP_SOURCES})
else()
    add_executable(ac6recomp ${AC6RECOMP_SOURCES})
endif()

rexglue_setup_target(ac6recomp)

# Link libmspack for AC6 PAC LZX decompression in the runtime dump path.
target_link_libraries(ac6recomp PRIVATE mspack)

if(AC6RECOMP_IPO_SUPPORTED)
    set_property(TARGET ac6recomp PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET ac6recomp PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
else()
    message(STATUS "IPO/LTO disabled for ac6recomp: ${AC6RECOMP_IPO_ERROR}")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ac6recomp PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fomit-frame-pointer>
    )
endif()
