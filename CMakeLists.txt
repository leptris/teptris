# teptris — Ultra-fast TOML 1.1 parser and emitter in C
# The TOML sibling of libleptris (XML) and libyeptris (YAML+JSON). Pure C
# library with CLI, tests and bindings, zero required runtime deps.
cmake_minimum_required(VERSION 3.20)

project(teptris
    VERSION 0.1.25
    DESCRIPTION "Ultra-fast TOML 1.1 parser and emitter in C"
    LANGUAGES C CXX
)

include(GNUInstallDirs)

# C99: the only C11-ism was _Static_assert, now a C99 typedef-array
# macro that MSVC's C mode also accepts. Widest compiler reach
# (same standard as libleptris).
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# C++11 standard for tests and benchmarks.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------- options --
option(BUILD_TESTING "Build tests" ON)
option(TEPTRIS_BUILD_CLI "Build the teptris CLI tool" ON)
option(TEPTRIS_BUILD_STATIC "Build the static library" ON)
option(TEPTRIS_BUILD_SHARED "Build the shared library" OFF)
option(TEPTRIS_BUILD_BENCHMARKS "Build performance benchmarks (TODO.impl/09)" OFF)
option(TEPTRIS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(TEPTRIS_ENABLE_TSAN "Enable ThreadSanitizer (requires all-TSAN build)" OFF)
option(TEPTRIS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(TEPTRIS_ENABLE_FUZZING "Build libFuzzer harnesses (TODO.impl/07; requires clang)" OFF)
option(TEPTRIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# LTO: ON for Release/RelWithDebInfo by default (libleptris convention).
if(NOT DEFINED TEPTRIS_ENABLE_LTO)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(TEPTRIS_ENABLE_LTO_DEFAULT ON)
    else()
        set(TEPTRIS_ENABLE_LTO_DEFAULT OFF)
    endif()
else()
    set(TEPTRIS_ENABLE_LTO_DEFAULT ${TEPTRIS_ENABLE_LTO})
endif()
option(TEPTRIS_ENABLE_LTO "Enable link-time optimization (default ON for Release)" ${TEPTRIS_ENABLE_LTO_DEFAULT})

# Merged llvm-profdata applied to teptris targets only (compile) and to
# consumers that link the LTO'd objects (ThinLTO consumes the profile at
# link time). Keeps benchmark competitors flag-free for fair ratios.
set(TEPTRIS_PROFILE_USE "" CACHE FILEPATH "Merged .profdata for -fprofile-use (teptris targets only)")
set(TEPTRIS_PROFILE_TRAIN OFF CACHE BOOL "Compile teptris targets with -fprofile-generate (training stage)")

# ------------------------------------------------------------- sanitizers --
if(TEPTRIS_ENABLE_ASAN AND TEPTRIS_ENABLE_TSAN)
    message(FATAL_ERROR "ASAN and TSAN are mutually exclusive")
endif()

if(TEPTRIS_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer: ENABLED")
endif()

if(TEPTRIS_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
    message(STATUS "ThreadSanitizer: ENABLED")
endif()

if(TEPTRIS_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
    message(STATUS "UndefinedBehaviorSanitizer: ENABLED")
endif()

if(TEPTRIS_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Library warnings: scoped to teptris targets so fetched dependencies
# (GTest) keep their own flags. Generator expressions keep MSVC happy
# (it rejects -Wall/-Wextra; leptris convention).
set(TEPTRIS_WARN_FLAGS
    $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:
        -Wall -Wextra -Wno-unused-parameter>
    $<$<C_COMPILER_ID:MSVC>: /W4 /wd4100>)
if(TEPTRIS_WARNINGS_AS_ERRORS)
    list(APPEND TEPTRIS_WARN_FLAGS
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:-Werror>
        $<$<C_COMPILER_ID:MSVC>:/WX>)
endif()

add_subdirectory(src)

if(TEPTRIS_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(TEPTRIS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

# ---- pkg-config consumer surface (TODO.impl/10) ----
configure_file(cmake/teptris.pc.in cmake/teptris.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/cmake/teptris.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
