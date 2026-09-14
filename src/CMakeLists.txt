# libteptris — core library sources.
#
# Subsystems land with their TODO.impl items (common+memory 02, parse 03,
# dom 04, emit 05).

set(TEPTRIS_SOURCES
    teptris/version.c
    teptris/common/chartype.c
    teptris/common/ryu/d2s.c
    teptris/common/ryu/s2d.c
    teptris/memory/arena.c
    teptris/dom/dom.c
    teptris/build.c
    teptris/parse/parser.c
    teptris/parse/keys.c
    teptris/parse/scalars.c
    teptris/parse/datetime.c
    teptris/emit/emitter.c
    teptris/api.c
)

set(teptris_pub_include ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(NOT TEPTRIS_BUILD_STATIC AND NOT TEPTRIS_BUILD_SHARED)
    message(FATAL_ERROR "At least one of TEPTRIS_BUILD_STATIC / TEPTRIS_BUILD_SHARED is required")
endif()

if(TEPTRIS_BUILD_STATIC)
    add_library(teptris STATIC ${TEPTRIS_SOURCES})
    target_include_directories(teptris PUBLIC $<BUILD_INTERFACE:${teptris_pub_include}>)
    target_include_directories(teptris PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(teptris PRIVATE ${TEPTRIS_WARN_FLAGS})
endif()

if(TEPTRIS_BUILD_SHARED)
    add_library(teptris_shared SHARED ${TEPTRIS_SOURCES})
    target_include_directories(teptris_shared PUBLIC $<BUILD_INTERFACE:${teptris_pub_include}>)
    target_include_directories(teptris_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(teptris_shared PRIVATE ${TEPTRIS_WARN_FLAGS})
    set_target_properties(teptris_shared PROPERTIES OUTPUT_NAME teptris
        VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
    # Public decls carry TEPTRIS_API; building the DLL marks them
    # dllexport, which generates the import library directly (works with
    # LTCG, unlike WINDOWS_EXPORT_ALL_SYMBOLS' generated .def).
    target_compile_definitions(teptris_shared PRIVATE TEPTRIS_BUILDING_DLL)
endif()
