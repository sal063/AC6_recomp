# Unit Tests for ReXGlue core components

add_executable(unit_tests
    memory/heap_allocation_test.cpp
    kernel/object_table_test.cpp
    kernel/xam_user_alias_test.cpp
    system/host_path_atomic_test.cpp
    core/cvar_test.cpp
    core/fiber_test.cpp
    core/sha256_test.cpp
    core/stream_test.cpp
    core/byte_order_test.cpp
    core/chrono_test.cpp
    codegen/config_test.cpp
    codegen/template_registry_test.cpp
)

target_include_directories(unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(unit_tests PRIVATE
    rexcore
    rexcodegen
    rexkernel
    rexfilesystem
    Catch2::Catch2WithMain
)

set_target_properties(unit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/bin
)

# Discover tests
include(Catch)
catch_discover_tests(unit_tests
    TEST_PREFIX "unit."
    PROPERTIES
        LABELS "unit"
        TIMEOUT 60
)

# Host wait-primitive semantics. Kept as its own binary because it needs only
# rexcore, so it stays runnable even when the wider unit_tests link is broken.
add_executable(sync_tests
    core/sync_semantics_test.cpp
)

target_include_directories(sync_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(sync_tests PRIVATE
    rexcore
    Catch2::Catch2WithMain
)

set_target_properties(sync_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/bin
)

catch_discover_tests(sync_tests
    TEST_PREFIX "sync."
    PROPERTIES
        LABELS "unit"
        TIMEOUT 120
)
