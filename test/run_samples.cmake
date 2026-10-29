# Both child statuses matter. CMake launches COMMANDs concurrently as a pipeline;
# samples don't consume stdin and print only small status summaries.
set(consumer_command "${CONSUMER}")
if(DEFINED RELEASE_SHIM)
    set(consumer_command "${CMAKE_COMMAND}" -E env "LD_PRELOAD=${RELEASE_SHIM}" "${CONSUMER}")
endif()
execute_process(COMMAND "${PRODUCER}" ${PRODUCER_ARGS}
                COMMAND ${consumer_command} ${CONSUMER_ARGS}
                RESULTS_VARIABLE results OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 25)
message(STATUS "${out}\n${err}")
list(GET results 0 producer_result)
list(GET results 1 consumer_result)
if(NOT DEFINED EXPECT_CONSUMER)
    set(EXPECT_CONSUMER 0)
endif()
if(NOT producer_result STREQUAL "0" OR NOT consumer_result STREQUAL "${EXPECT_CONSUMER}")
    message(FATAL_ERROR "sample process statuses: ${results}; expected 0;${EXPECT_CONSUMER}")
endif()
if(EXPECT_CONSUMER EQUAL 1 AND NOT err MATCHES "pixel mismatch")
    message(FATAL_ERROR "negative test did not detect pixel corruption")
endif()

if(DEFINED RELEASE_SHIM AND NOT err MATCHES "injected released-peer disconnect")
    message(FATAL_ERROR "release disconnect injection was not exercised")
endif()

if(EXPECT_CONSUMED_INJECTION AND NOT err MATCHES "injected consumed-peer disconnect")
    message(FATAL_ERROR "consumed disconnect injection was not exercised")
endif()
