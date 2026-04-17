# Native UI runtime - phase 6A presenter/window layer

set(REXUI_CORE_SOURCES
    graphics_upload_buffer_pool.cpp
    graphics_util.cpp
    ${REXGLUE_ROOT}/src/ui/imgui_dialog.cpp
    ${REXGLUE_ROOT}/src/ui/imgui_drawer.cpp
    immediate_drawer.cpp
    ${REXGLUE_ROOT}/src/ui/keybinds.cpp
    ${REXGLUE_ROOT}/src/ui/menu_item.cpp
    presenter.cpp
    renderdoc_api.cpp
    window.cpp
    windowed_app.cpp
    windowed_app_context.cpp
)

set(REXUI_OVERLAY_SOURCES
    ${REXGLUE_ROOT}/src/ui/overlay/debug_overlay.cpp
    ${REXGLUE_ROOT}/src/ui/overlay/console_overlay.cpp
    ${REXGLUE_ROOT}/src/ui/overlay/settings_overlay.cpp
)

set(REXUI_VULKAN_SOURCES
    ${REXGLUE_ROOT}/src/ui/vulkan/linked_type_descriptor_set_allocator.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/single_layout_descriptor_set_pool.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/spirv_tools_context.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/ui_samplers.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_device.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_immediate_drawer.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_instance.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_mem_alloc.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_presenter.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_provider.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_submission_tracker.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_upload_buffer_pool.cpp
    ${REXGLUE_ROOT}/src/ui/vulkan/vulkan_util.cpp
)

if(REXGLUE_USE_D3D12)
    set(REXUI_D3D12_SOURCES
        d3d12/d3d12_cpu_descriptor_pool.cpp
        d3d12/d3d12_descriptor_heap_pool.cpp
        d3d12/d3d12_immediate_drawer.cpp
        d3d12/d3d12_presenter.cpp
        d3d12/d3d12_provider.cpp
        d3d12/d3d12_submission_tracker.cpp
        d3d12/d3d12_upload_buffer_pool.cpp
        d3d12/d3d12_util.cpp
    )
endif()

if(WIN32)
    set(REXUI_PLATFORM_SOURCES
        surface_win.cpp
        window_win.cpp
        windowed_app_context_win.cpp
    )
else()
    set(REXUI_PLATFORM_SOURCES
        surface_gnulinux.cpp
        window_gtk.cpp
        windowed_app_context_gtk.cpp
    )
endif()

set(REXUI_SOURCES
    ${REXUI_CORE_SOURCES}
    ${REXUI_OVERLAY_SOURCES}
    ${REXUI_PLATFORM_SOURCES}
)

if(REXGLUE_USE_VULKAN)
    list(APPEND REXUI_SOURCES ${REXUI_VULKAN_SOURCES})
endif()

if(REXGLUE_USE_D3D12)
    list(APPEND REXUI_SOURCES ${REXUI_D3D12_SOURCES})
endif()

add_library(rexui STATIC ${REXUI_SOURCES})
add_library(rex::ui ALIAS rexui)

target_include_directories(rexui PUBLIC
    $<BUILD_INTERFACE:${REXGLUE_ROOT}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(rexui PRIVATE
    ${REXGLUE_ROOT}/src
)

target_link_libraries(rexui PUBLIC
    rexcore
    renderdoc
    imgui::imgui
)

if(REXGLUE_FIDELITYFX_SOURCE_DIR AND EXISTS "${REXGLUE_FIDELITYFX_SOURCE_DIR}/sdk/include")
    target_include_directories(rexui PRIVATE
        "${REXGLUE_FIDELITYFX_SOURCE_DIR}/sdk/include"
        "${REXGLUE_FIDELITYFX_SOURCE_DIR}/ffx-api/include"
    )
    target_compile_definitions(rexui PRIVATE REX_HAS_FIDELITYFX_SDK=1)
endif()

set(REXUI_HAS_FIDELITYFX_RUNTIME OFF)
if(TARGET amd_fidelityfx_vk)
    target_link_libraries(rexui PRIVATE amd_fidelityfx_vk)
    set(REXUI_HAS_FIDELITYFX_RUNTIME ON)
endif()
if(TARGET amd_fidelityfx_dx12)
    target_link_libraries(rexui PRIVATE amd_fidelityfx_dx12)
    set(REXUI_HAS_FIDELITYFX_RUNTIME ON)
endif()
if(REXUI_HAS_FIDELITYFX_RUNTIME)
    target_compile_definitions(rexui PRIVATE REX_HAS_FIDELITYFX_RUNTIME=1)
endif()

if(REXGLUE_USE_VULKAN)
    target_link_libraries(rexui PUBLIC
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        SPIRV-Tools-static
    )
endif()

if(WIN32)
    target_link_libraries(rexui PUBLIC
        dwmapi
        Shcore
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(X11_XCB REQUIRED x11-xcb)

    target_include_directories(rexui PRIVATE
        ${GTK3_INCLUDE_DIRS}
        ${X11_XCB_INCLUDE_DIRS}
    )
    target_link_libraries(rexui PUBLIC
        ${GTK3_LIBRARIES}
        ${X11_XCB_LIBRARIES}
    )
endif()

if(REXGLUE_USE_D3D12)
    target_link_libraries(rexui PUBLIC
        dxgi
        dxc-headers
    )
endif()
