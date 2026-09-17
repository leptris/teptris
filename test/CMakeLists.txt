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
    unit/test_emit.cpp
    unit/test_corpus.cpp
    unit/test_builder.cpp
    unit/test_flatten.cpp
    unit/test_plan.cpp
)
target_link_libraries(test_unit PRIVATE teptris GTest::gtest_main)
target_include_directories(test_unit PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(test_unit DISCOVERY_TIMEOUT 60)
