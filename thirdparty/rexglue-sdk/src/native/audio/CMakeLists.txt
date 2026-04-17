# rexaudio - Native audio subsystem library (WASAPI-only, Windows target)

add_library(rexaudio STATIC
    audio_driver.cpp
    audio_clock.cpp
    audio_runtime.cpp
    audio_system.cpp
    audio_trace.cpp
    wasapi/wasapi_audio_driver.cpp
    xma/context.cpp
    xma/decoder.cpp
    xma/register_file.cpp
    xma/xma_decoder_backend.cpp
)
add_library(rex::audio ALIAS rexaudio)

target_include_directories(rexaudio PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(rexaudio
    PUBLIC rexcore libavcodec libavutil ole32 avrt uuid
)
