# cli/ — the teptris command-line tool (TODO.impl/01).

add_executable(teptris_cli main.c)
# whichever lib flavor is enabled carries the public include path
if(TARGET teptris_shared)
    target_link_libraries(teptris_cli PRIVATE teptris_shared)
else()
    target_link_libraries(teptris_cli PRIVATE teptris)
endif()
target_compile_options(teptris_cli PRIVATE ${TEPTRIS_WARN_FLAGS})
set_target_properties(teptris_cli PROPERTIES OUTPUT_NAME teptris)
# schema-validate walks cap at 512 ref hops; Windows' 1MB default
# stack sits at the edge of that frame budget under MSVC/LTO
if(MSVC)
    target_link_options(teptris_cli PRIVATE "/STACK:8388608")
endif()

# the tool belongs on the PATH next to the library (cmake --install)
install(TARGETS teptris_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
