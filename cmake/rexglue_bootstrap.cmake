# Bootstrap ReXGlue integration for fresh clones where generated/rexglue.cmake
# is not present yet.
#
# This file is intentionally tracked in git so first-time configure works on
# all platforms before running codegen.

# SDK version
# Set REXSDK_VERSION to pin a specific SDK version.
# Otherwise, the version bundled with this bootstrap is used.
set(REXSDK_VERSION "" CACHE STRING "Override SDK version (leave empty for default)")

# Find SDK
set(REXSDK_DIR "" CACHE PATH "Path to rexglue-sdk source tree")
if(REXSDK_DIR)
    add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
else()
    if(REXSDK_VERSION)
        find_package(rexglue ${REXSDK_VERSION} EXACT QUIET CONFIG)
    else()
        find_package(rexglue 0.7.4 QUIET CONFIG)
    endif()
    if(NOT rexglue_FOUND)
        message(FATAL_ERROR
            "ReXGlue SDK not found. Either:\n"
            "  - Set REXSDK_DIR to the rexglue-sdk source tree (e.g. thirdparty/rexglue-sdk)\n"
            "  - Install the SDK package and ensure it is on CMAKE_PREFIX_PATH")
    endif()
endif()

# Include generated code if codegen has been run.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/sources.cmake")
    include(generated/sources.cmake)
endif()

# Configure a rexglue target with SDK libraries and platform settings.
# Call after add_executable() in your CMakeLists.txt.
# Usage: rexglue_setup_target(<target>)
macro(rexglue_setup_target target_name)
    target_sources(${target_name} PRIVATE ${GENERATED_SOURCES})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
    )
    target_link_libraries(${target_name} PRIVATE
        rex::core
        rex::system
        rex::kernel
        rex::graphics
        rex::ui
    )
    rexglue_configure_target(${target_name})
endmacro()

# Codegen target - run:
# cmake --build . --target ac6recomp_codegen
add_custom_target(ac6recomp_codegen
    COMMAND $<TARGET_FILE:rex::rexglue> codegen ${CMAKE_CURRENT_SOURCE_DIR}/ac6recomp_config.toml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating recompiled code for ac6recomp"
    VERBATIM
)
