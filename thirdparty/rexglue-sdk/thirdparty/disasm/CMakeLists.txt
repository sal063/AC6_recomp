project("disasm")

add_library(disasm "ppc-dis.c" "disasm.c" "ppc.h")
target_include_directories(disasm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
