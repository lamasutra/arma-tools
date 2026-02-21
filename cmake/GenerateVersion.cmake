# Executed at build time via add_custom_target to pick up git state changes.
# Variables passed in: SOURCE_DIR, BINARY_DIR, PROJECT_VERSION,
# PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH

find_package(Git QUIET)

set(GIT_HASH "")
set(GIT_DIRTY 0)

if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE git_result
    )
    if(NOT git_result EQUAL 0)
        set(GIT_HASH "")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" diff-index --quiet HEAD --
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE git_dirty_result
    )
    if(NOT git_dirty_result EQUAL 0)
        set(GIT_DIRTY 1)
    endif()
endif()

configure_file(
    "${SOURCE_DIR}/cmake/version.h.in"
    "${BINARY_DIR}/generated/armatools/version.h"
    @ONLY
)
