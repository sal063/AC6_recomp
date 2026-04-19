# ac6recomp - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ac6recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake")
    include(generated/rexglue.cmake)
else()
    include(cmake/rexglue_bootstrap.cmake)
endif()

# Sources
set(AC6RECOMP_SOURCES
    src/main.cpp
    src/d3d_hooks.cpp
    src/render_hooks.cpp
    src/ac6_native_graphics.cpp
    src/ac6_native_graphics_overlay.cpp
    src/ac6_backend_fixes/ac6_backend_capture_bridge.cpp
    src/ac6_backend_fixes/ac6_backend_hooks.cpp
    src/ac6_backend_fixes/ac6_backend_pass_classifier.cpp
    src/ac6_native_renderer/ac6_render_frontend.cpp
    src/ac6_native_renderer/execution_plan.cpp
    src/ac6_native_renderer/frame_plan.cpp
    src/ac6_native_renderer/replay_ir.cpp
    src/ac6_native_renderer/replay_executor.cpp
    src/ac6_native_renderer/backends/backend_factory.cpp
    src/ac6_native_renderer/backends/d3d12_backend.cpp
    src/ac6_native_renderer/backends/d3d12_resource_manager.cpp
    src/ac6_native_renderer/backends/d3d12_resource_tracker.cpp
    src/ac6_native_renderer/backends/d3d12_shader_manager.cpp
    src/ac6_native_renderer/backends/metal_backend.cpp
    src/ac6_native_renderer/backends/vulkan_backend.cpp
    src/ac6_native_renderer/frame_scheduler.cpp
    src/ac6_native_renderer/native_renderer.cpp
    src/ac6_native_renderer/render_device.cpp
    src/ac6_native_renderer/render_graph.cpp
)

if(WIN32)
    add_executable(ac6recomp WIN32 ${AC6RECOMP_SOURCES})
else()
    add_executable(ac6recomp ${AC6RECOMP_SOURCES})
endif()

rexglue_setup_target(ac6recomp)
