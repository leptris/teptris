# cli/ — the teptris command-line tool (TODO.impl/01).

add_executable(teptris_cli main.c)
target_link_libraries(teptris_cli PRIVATE teptris)
target_compile_options(teptris_cli PRIVATE ${TEPTRIS_WARN_FLAGS})
set_target_properties(teptris_cli PROPERTIES OUTPUT_NAME teptris)
