# rexaudio - Native audio subsystem library

add_library(rexaudio STATIC
    audio_driver.cpp
    audio_clock.cpp
    render_driver_frame_layout.cpp
    audio_runtime.cpp
    audio_system.cpp
    audio_trace.cpp
    xma/context.cpp
    xma/decoder.cpp
    xma/register_file.cpp
    xma/xma_decoder_backend.cpp
)
add_library(rex::audio ALIAS rexaudio)

if(WIN32)
    target_sources(rexaudio PRIVATE
        wasapi/wasapi_audio_driver.cpp
        sdl/sdl_audio_driver.cpp
    )
else()
    target_sources(rexaudio PRIVATE
        sdl/sdl_audio_driver.cpp
    )
endif()

target_include_directories(rexaudio PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(rexaudio
    PUBLIC rexcore libavcodec libavutil
)

if(WIN32)
    target_link_libraries(rexaudio PUBLIC ole32 avrt uuid SDL3::SDL3)
else()
    target_link_libraries(rexaudio PUBLIC SDL3::SDL3)
endif()
