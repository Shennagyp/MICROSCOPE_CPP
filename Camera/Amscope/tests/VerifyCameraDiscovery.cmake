if(NOT DEFINED PREVIEW_EXECUTABLE OR NOT DEFINED GENICAM_GENTL_PATH)
    message(FATAL_ERROR "PREVIEW_EXECUTABLE and GENICAM_GENTL_PATH are required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "GENICAM_GENTL64_PATH=${GENICAM_GENTL_PATH}"
        "${PREVIEW_EXECUTABLE}" --list-cameras
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 45)

message(STATUS "Combined camera discovery output:\n${output}")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
        "Combined camera discovery failed (result=${result}):\n${error}")
endif()

if(NOT output MATCHES "AmScope devices: [0-9]+")
    message(FATAL_ERROR "The combined scan did not report the AmScope backend.")
endif()

if(NOT output MATCHES "GenTL devices: 2")
    message(FATAL_ERROR
        "The combined scan did not discover both devices from the in-repository CTI simulator.")
endif()

if(NOT output MATCHES "Total cameras found: [0-9]+")
    message(FATAL_ERROR "The combined scan did not report its total device count.")
endif()
