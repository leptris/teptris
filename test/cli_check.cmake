# cli_check.cmake — cross-platform CLI smoke assertions for ctest
# (script mode; no shell, no popen). Each case runs the built binary,
# captures stdout/rc, and compares against expected values.
#   cmake -DCLI=<path> -DCASE=<case> -DFILE=<fixture> -P cli_check.cmake
if(NOT CLI OR NOT CASE)
    message(FATAL_ERROR "CLI and CASE are required")
endif()

set(INPUT "title = \"demo\"\npi = 3.5\non = true\nat = 2026-10-09T12:00:00Z\nnan = nan\n")
file(WRITE "${CMAKE_BINARY_DIR}/cli_input.toml" "${INPUT}")

if(CASE STREQUAL "version")
    execute_process(COMMAND "${CLI}" version OUTPUT_VARIABLE OUT
                    RESULT_VARIABLE RC OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT RC EQUAL 0 OR NOT OUT MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "version: rc=${RC} out=${OUT}")
    endif()
elseif(CASE STREQUAL "to_json_natural")
    execute_process(COMMAND "${CLI}" to-json --natural "${CMAKE_BINARY_DIR}/cli_input.toml"
                    OUTPUT_VARIABLE OUT RESULT_VARIABLE RC)
    if(NOT RC EQUAL 0)
        message(FATAL_ERROR "to-json --natural: rc=${RC}")
    endif()
    if(NOT OUT MATCHES "\"pi\":3\\.5" OR NOT OUT MATCHES "\"on\":true"
       OR NOT OUT MATCHES "\"at\":\"2026-10-09T12:00:00Z\""
       OR NOT OUT MATCHES "\"nan\":null" OR NOT OUT MATCHES "\"title\":\"demo\"")
        message(FATAL_ERROR "natural shape wrong: ${OUT}")
    endif()
    if(OUT MATCHES "\"type\"")
        message(FATAL_ERROR "natural output must not carry conformance tags: ${OUT}")
    endif()
elseif(CASE STREQUAL "to_json_conformance")
    execute_process(COMMAND "${CLI}" to-json "${CMAKE_BINARY_DIR}/cli_input.toml"
                    OUTPUT_VARIABLE OUT RESULT_VARIABLE RC)
    if(NOT RC EQUAL 0)
        message(FATAL_ERROR "to-json: rc=${RC}")
    endif()
    if(NOT OUT MATCHES "\"type\":\"float\"")
        message(FATAL_ERROR "conformance dialect missing tags: ${OUT}")
    endif()
elseif(CASE STREQUAL "natural_rejects_on_format")
    execute_process(COMMAND "${CLI}" format --natural
                    OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR RESULT_VARIABLE RC)
    if(RC EQUAL 0)
        message(FATAL_ERROR "format --natural must be rejected (rc=2)")
    endif()
else()
    message(FATAL_ERROR "unknown CASE ${CASE}")
endif()
