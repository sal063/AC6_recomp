# rexnativefilesystem - Native filesystem / mount layer
# Replaces the legacy Xenia-derived VFS abstraction

add_library(rexfilesystem STATIC
    device.cpp
    entry.cpp
    virtual_file_system.cpp
    devices/host_path_device.cpp
    devices/host_path_entry.cpp
    devices/host_path_file.cpp
    devices/null_device.cpp
    devices/null_entry.cpp
    devices/null_file.cpp
)
add_library(rex::filesystem ALIAS rexfilesystem)

target_include_directories(rexfilesystem PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(rexfilesystem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/devices)

target_link_libraries(rexfilesystem PUBLIC rexcore)
