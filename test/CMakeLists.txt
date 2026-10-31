# test/ — Google Test binaries (TODO.impl/01/07).

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GTest not found locally — fetching googletest v1.17.0")
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_executable(test_unit
    unit/test_internal.cpp
    unit/test_scalars.cpp
    unit/test_datetime.cpp
    unit/test_tables.cpp
    unit/test_errors.cpp
    unit/test_edit.cpp
    unit/test_emit.cpp
    unit/test_writer.cpp
    unit/test_corpus.cpp
    unit/test_builder.cpp
    unit/test_flatten.cpp
    unit/test_plan.cpp
    unit/test_parse_batch.cpp
)
target_link_libraries(test_unit PRIVATE teptris GTest::gtest_main)
target_include_directories(test_unit PRIVATE ${CMAKE_SOURCE_DIR}/src)
# E6: the canonical-form property test reads the checked-in corpus
target_compile_definitions(test_unit PRIVATE
    TEPTRIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(test_unit DISCOVERY_TIMEOUT 60)

# CLI smoke: the binary ships in every CI lane (TEPTRIS_BUILD_CLI=ON);
# these ctest cases assert exit codes and output shapes without a
# shell (cmake -P script mode runs on every platform).
if(TARGET teptris_cli)
    foreach(case version to_json_natural to_json_conformance natural_rejects_on_format set_preserves_formatting set_string_and_missing)
        add_test(NAME cli_${case}
                 COMMAND ${CMAKE_COMMAND}
                         -DCLI=$<TARGET_FILE:teptris_cli>
                         -DCASE=${case}
                         -P ${CMAKE_SOURCE_DIR}/test/cli_check.cmake)
    endforeach()
endif()
