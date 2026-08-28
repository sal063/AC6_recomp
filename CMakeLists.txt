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
    src/ac6_texture_swap_cvars.cpp
    src/ac6_native_graphics.cpp
    src/ac6_native_graphics_overlay.cpp
    src/ac6_backend_fixes/ac6_backend_capture_bridge.cpp
    src/ac6_backend_fixes/ac6_backend_hooks.cpp
    src/ac6_backend_fixes/ac6_backend_pass_classifier.cpp
    src/ac6_backend_fixes/ac6_cutscene_resync.cpp
    src/ac6_backend_fixes/ac6_effect_mode_fix.cpp
    src/ac6_backend_fixes/ac6_fps_physics_fix.cpp
    src/ac6_backend_fixes/ac6_fullres_effects.cpp
    src/ac6_backend_fixes/ac6_kbm_input.cpp
    src/ac6_backend_fixes/ac6_storage_submit_order_fix.cpp
    src/ac6_backend_fixes/ac6_widescreen.cpp
)

# Texture swapping is reachable only from the D3D12 texture cache, which is not
# built on non-Windows hosts, so the translation unit would have no callers there.
if(WIN32)
    list(APPEND AC6RECOMP_SOURCES src/ac6_texture_overrides.cpp)
endif()

if(WIN32)
    add_executable(ac6recomp WIN32 ${AC6RECOMP_SOURCES})
else()
    add_executable(ac6recomp ${AC6RECOMP_SOURCES})
endif()

rexglue_setup_target(ac6recomp)

# The SDK's xmemory.cpp reads cvars owned by graphics/flags.cpp while rexgraphics
# already links rexsystem, so the two static archives are mutually dependent.
# Declaring the back-edge makes CMake repeat them on the link line. On Windows the
# symbol resolves by accident, because d3d12/command_processor.cpp references the
# same cvar and pulls flags.cpp.o in ahead of rexsystem; a Vulkan-only build has no
# such earlier reference and the rexglue tool fails to link. Applied here rather
# than in the SDK to keep the vendored tree pristine.
if(TARGET rexsystem AND TARGET rexgraphics)
    target_link_libraries(rexsystem PUBLIC rexgraphics)
endif()

# Link libmspack for AC6 PAC LZX decompression in the runtime dump path.
target_link_libraries(ac6recomp PRIVATE mspack)

# The input driver looks for the gamepad mappings next to the executable, but the
# binary stays in the build tree rather than an install directory, so put a copy
# there on every build.
add_custom_command(TARGET ac6recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/gamecontrollerdb.txt"
        "$<TARGET_FILE_DIR:ac6recomp>"
    COMMENT "Copying gamecontrollerdb.txt next to ac6recomp"
)

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
