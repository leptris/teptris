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
