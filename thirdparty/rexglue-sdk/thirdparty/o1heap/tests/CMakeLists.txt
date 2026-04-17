# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or substantial portions
# of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
# OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Copyright (c) 2020 Pavel Kirienko
# Authors: Pavel Kirienko <pavel.kirienko@zubax.com>

cmake_minimum_required(VERSION 3.12)
project(o1heap_tests C CXX)
enable_testing()

if (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    # assert() shall be disabled in release builds to enable testing of bad free() calls.
    add_definitions(-DNDEBUG=1)
endif ()

set(library_dir "${CMAKE_SOURCE_DIR}/o1heap")

# clang-tidy
set(DISABLE_CLANG_TIDY OFF CACHE BOOL "Do not use Clang-Tidy")
if (NOT DISABLE_CLANG_TIDY)
    # clang-tidy (separate config files per directory)
    find_program(clang_tidy NAMES clang-tidy)
    if (NOT clang_tidy)
        message(FATAL_ERROR "Could not locate clang-tidy")
    endif ()
    message(STATUS "Using clang-tidy: ${clang_tidy}")
    set(CMAKE_C_CLANG_TIDY ${clang_tidy})
    set(CMAKE_CXX_CLANG_TIDY ${clang_tidy})
endif ()

# clang-format
find_program(clang_format NAMES clang-format)
if (NOT clang_format)
    message(STATUS "Could not locate clang-format")
else ()
    file(GLOB format_files ${library_dir}/*.[ch] ${CMAKE_CURRENT_SOURCE_DIR}/*.[ch]pp)
    message(STATUS "Using clang-format: ${clang_format}; files: ${format_files}")
    add_custom_target(format COMMAND ${clang_format} -i -fallback-style=none -style=file --verbose ${format_files})
endif ()

# C options
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic -fstrict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wdouble-promotion -Wswitch-enum -Wfloat-equal -Wundef")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wtype-limits")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wsign-conversion -Wcast-align -Wmissing-declarations")

# C++ options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -pedantic -fstrict-aliasing")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdouble-promotion -Wswitch-enum -Wfloat-equal -Wundef")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion -Wsign-promo")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-conversion -Wcast-align -Wmissing-declarations")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wtype-limits -Wzero-as-null-pointer-constant -Wnon-virtual-dtor")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Woverloaded-virtual -Wsign-promo -Wold-style-cast")

include_directories(SYSTEM catch)
include_directories(${library_dir})

set(common_sources ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${library_dir}/o1heap.c)

function(gen_test name files compile_definitions compile_features compile_flags link_flags)
    add_executable(${name} ${common_sources} ${files})
    target_compile_definitions(${name} PUBLIC ${compile_definitions})
    target_compile_features(${name} PUBLIC ${compile_features})
    set_target_properties(${name} PROPERTIES COMPILE_FLAGS "${compile_flags}" LINK_FLAGS "${link_flags}")
    add_test("run_${name}" "${name}" --rng-seed time)
endfunction()

function(gen_test_matrix name files defs)
    gen_test("${name}_c99_x64"      "${files}" "${defs}"                            c_std_99 "-m64" "-m64")
    gen_test("${name}_c99_x32"      "${files}" "${defs}"                            c_std_99 "-m32" "-m32")
    gen_test("${name}_c11_x64"      "${files}" "${defs}"                            c_std_11 "-m64" "-m64")
    gen_test("${name}_c11_x32"      "${files}" "${defs}"                            c_std_11 "-m32" "-m32")
    gen_test("${name}_c11_x64_ni"   "${files}" "${defs};O1HEAP_USE_INTRINSICS=0"    c_std_11 "-m64" "-m64")
    gen_test("${name}_c11_x32_ni"   "${files}" "${defs};O1HEAP_USE_INTRINSICS=0"    c_std_11 "-m32" "-m32")
    # Coverage is only available for GCC builds.
    if ((CMAKE_CXX_COMPILER_ID STREQUAL "GNU") AND (CMAKE_BUILD_TYPE STREQUAL "Debug"))
        gen_test("${name}_cov" "${files}" "${defs}" c_std_11 "-g -O0 --coverage" "--coverage")
    endif ()
endfunction()

gen_test_matrix(
        test_private
        test_private.cpp
        "O1HEAP_CONFIG_HEADER=\"${CMAKE_CURRENT_SOURCE_DIR}/cfg_test_internal.h\""
)
gen_test_matrix(
        test_general
        test_general.cpp
        ""
)

gen_test("test_api_exhaustive_c11_x64" "test_api_exhaustive.cpp" "" c_std_11 "-m64" "-m64")
gen_test("test_api_exhaustive_c11_x32" "test_api_exhaustive.cpp" "" c_std_11 "-m32" "-m32")
