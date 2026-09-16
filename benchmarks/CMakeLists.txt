# benchmarks/ — TODO.impl/09. Parse matrix vs the reference C/C++ TOML
# libraries, pinned in $TEPTRIS_EXTERNAL_DIR (default ~/src/external):
#   tomlc99       (C,      HEAD 29076df 2026-01-30)
#   toml11        (C++17,  v4.2.0)
#   tomlplusplus  (C++17,  v3.4.0)
# Libraries found locally are wired in; missing ones are skipped.

set(TEPTRIS_EXTERNAL_DIR "$ENV{HOME}/src/external" CACHE PATH
    "Reference TOML libraries root (tomlc99, toml11, tomlplusplus)")

set(BENCH_SOURCES bench_main.cpp)
set(BENCH_DEFS "")

if(EXISTS "${TEPTRIS_EXTERNAL_DIR}/tomlc99/toml.c")
    add_library(tomlc99 STATIC "${TEPTRIS_EXTERNAL_DIR}/tomlc99/toml.c")
    target_include_directories(tomlc99 PUBLIC "${TEPTRIS_EXTERNAL_DIR}/tomlc99")
    list(APPEND BENCH_SOURCES bench_tomlc99.cpp)
    list(APPEND BENCH_DEFS HAVE_TOMLC99=1)
else()
    message(STATUS "benchmarks: tomlc99 not found — skipped")
endif()

if(EXISTS "${TEPTRIS_EXTERNAL_DIR}/toml11/single_include/toml.hpp")
    list(APPEND BENCH_SOURCES bench_toml11.cpp)
    list(APPEND BENCH_DEFS HAVE_TOML11=1)
else()
    message(STATUS "benchmarks: toml11 not found — skipped")
endif()

if(EXISTS "${TEPTRIS_EXTERNAL_DIR}/tomlplusplus/toml.hpp")
    list(APPEND BENCH_SOURCES bench_tomlplus.cpp)
    list(APPEND BENCH_DEFS HAVE_TOMLPLUS=1)
else()
    message(STATUS "benchmarks: tomlplusplus not found — skipped")
endif()

if(EXISTS "${TEPTRIS_EXTERNAL_DIR}/tomlc17/src/tomlc17.c")
    add_library(tomlc17 STATIC "${TEPTRIS_EXTERNAL_DIR}/tomlc17/src/tomlc17.c")
    target_include_directories(tomlc17 PUBLIC "${TEPTRIS_EXTERNAL_DIR}/tomlc17/src")
    # tomlc99 exports the same toml_parse/toml_free names with different
    # signatures; rename tomlc17's entries to keep both linkable.
    target_compile_definitions(tomlc17 PRIVATE
        toml_parse=tepc17_parse toml_parse_file=tepc17_parse_file
        toml_free=tepc17_free)
    list(APPEND BENCH_SOURCES bench_tomlc17.cpp)
    list(APPEND BENCH_DEFS HAVE_TOMLC17=1)
else()
    message(STATUS "benchmarks: tomlc17 not found — skipped")
endif()

if(EXISTS "${TEPTRIS_EXTERNAL_DIR}/cpptoml/include/cpptoml.h")
    list(APPEND BENCH_SOURCES bench_cpptoml.cpp)
    list(APPEND BENCH_DEFS HAVE_CPPTOML=1)
    list(APPEND BENCH_EXTRA_INCLUDES "${TEPTRIS_EXTERNAL_DIR}/cpptoml/include")
else()
    message(STATUS "benchmarks: cpptoml not found — skipped")
endif()

configure_file(bench_paths.h.in bench_paths.h @ONLY)

add_executable(bench_parse ${BENCH_SOURCES})

add_executable(bench_emit bench_emit.cpp)
target_link_libraries(bench_emit PRIVATE teptris)
# ThinLTO consumes the profile at link time: the teptris objects riding
# into bench_parse need -fprofile-use on this link too.
if(TEPTRIS_PROFILE_USE)
    target_link_options(bench_parse PRIVATE
        -fprofile-use=${TEPTRIS_PROFILE_USE} -fprofile-correction)
endif()
target_link_libraries(bench_parse PRIVATE teptris)
if(TARGET tomlc99)
    target_link_libraries(bench_parse PRIVATE tomlc99)
endif()
if(TARGET tomlc17)
    target_link_libraries(bench_parse PRIVATE tomlc17)
endif()
target_include_directories(bench_parse PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    ${BENCH_EXTRA_INCLUDES})
target_compile_definitions(bench_parse PRIVATE ${BENCH_DEFS})
set_target_properties(bench_parse PROPERTIES CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(bench_parse PRIVATE -O2)
