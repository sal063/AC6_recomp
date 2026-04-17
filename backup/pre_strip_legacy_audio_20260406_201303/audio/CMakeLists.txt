# rexaudio - Audio subsystem library
# Placeholder audio surface used while the project rewrites audio from scratch.

add_library(rexaudio STATIC
    audio_driver.cpp
    audio_clock.cpp
    audio_runtime.cpp
    audio_system.cpp
    audio_trace.cpp
    host/null_audio_backend.cpp
    nop/nop_audio_system.cpp
    sdl/sdl_audio_driver.cpp
    sdl/sdl_audio_system.cpp
    xma/context.cpp
    xma/decoder.cpp
    xma/register_file.cpp
    xma/xma_context_pool.cpp
    xma/xma_decoder_backend.cpp
    xma/xma_packet_parser.cpp
)
add_library(rex::audio ALIAS rexaudio)

if(WIN32)
    target_sources(rexaudio PRIVATE wasapi/wasapi_audio_driver.cpp)
endif()

target_include_directories(rexaudio PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(rexaudio
    PUBLIC rexcore SDL3::SDL3 libavcodec libavutil
)

if(WIN32)
    target_link_libraries(rexaudio PUBLIC ole32 avrt uuid)
endif()
