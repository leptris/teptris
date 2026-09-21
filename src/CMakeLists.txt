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
    teptris/plan/plan.c
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
    target_include_directories(teptris PUBLIC
        $<BUILD_INTERFACE:${teptris_pub_include}>
        $<INSTALL_INTERFACE:include>)
    target_include_directories(teptris PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(teptris PRIVATE ${TEPTRIS_WARN_FLAGS})
    list(APPEND TEPTRIS_INSTALL_TARGETS teptris)
endif()


if(TEPTRIS_BUILD_SHARED)
    add_library(teptris_shared SHARED ${TEPTRIS_SOURCES})
    target_include_directories(teptris_shared PUBLIC
        $<BUILD_INTERFACE:${teptris_pub_include}>
        $<INSTALL_INTERFACE:include>)
    target_include_directories(teptris_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(teptris_shared PRIVATE ${TEPTRIS_WARN_FLAGS})
    set_target_properties(teptris_shared PROPERTIES OUTPUT_NAME teptris
        VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
    list(APPEND TEPTRIS_INSTALL_TARGETS teptris_shared)
    # Public decls carry TEPTRIS_API; building the DLL marks them
    # dllexport, which generates the import library directly (works with
    # LTCG, unlike WINDOWS_EXPORT_ALL_SYMBOLS' generated .def).
    target_compile_definitions(teptris_shared PRIVATE TEPTRIS_BUILDING_DLL)
endif()

# PGO knobs live after both library targets exist so each gets them.
# TRAIN: compile-only instrumentation plus an INTERFACE link flag so
# consumers (the CLI trainer, harnesses) pull the profile runtime.
# USE: teptris targets only — benchmark competitors stay flag-free.
# MSVC: /GL + /GENPROFILE then /USEPROFILE; the .pgd rides beside the
# link output, so USE ignores the path value (pass ON).
if(TEPTRIS_PROFILE_USE)
    foreach(tgt teptris teptris_shared)
        if(TARGET ${tgt})
            if(MSVC)
                target_compile_options(${tgt} PRIVATE /GL)
                target_link_options(${tgt} PRIVATE /LTCG /USEPROFILE)
            else()
                target_compile_options(${tgt} PRIVATE
                    -fprofile-use=${TEPTRIS_PROFILE_USE} -fprofile-correction)
            endif()
        endif()
    endforeach()
endif()
if(TEPTRIS_PROFILE_TRAIN)
    foreach(tgt teptris teptris_shared)
        if(TARGET ${tgt})
            if(MSVC)
                target_compile_options(${tgt} PRIVATE /GL)
                target_link_options(${tgt} PRIVATE /LTCG /GENPROFILE)
            else()
                target_compile_options(${tgt} PRIVATE -fprofile-generate)
                # PRIVATE: the shared dylib's own link needs the runtime too
                # (the static archive never links itself)
                target_link_options(${tgt} PRIVATE -fprofile-generate)
                target_link_options(${tgt} INTERFACE -fprofile-generate)
            endif()
        endif()
    endforeach()
endif()

# ---- install + consumer surface (TODO.impl/10) ----
install(TARGETS ${TEPTRIS_INSTALL_TARGETS}
        EXPORT teptris-targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/teptris
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT teptris-targets
        NAMESPACE teptris::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/teptris)
