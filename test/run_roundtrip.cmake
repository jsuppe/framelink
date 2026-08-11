# Drives the two-process round trip for ctest.
#
# Listing two COMMANDs makes execute_process run them CONCURRENTLY (it builds a
# pipeline; the pipe itself is unused here). That is the only portable way to
# get two live processes out of CMake, and it means the producer may start
# before the consumer has created the channel - hence fl_produce's attach
# retry. The consumer's exit code is the verdict, because it is the one
# checking pixels.

execute_process(
    COMMAND "${PRODUCER}" ${PRODUCER_ARGS}
    COMMAND "${CONSUMER}" ${CONSUMER_ARGS}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    TIMEOUT 90)

message(STATUS "${out}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "round trip failed (rc=${rc})\n${out}\n${err}")
endif()
