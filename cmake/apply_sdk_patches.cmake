# apply_sdk_patches.cmake
#
# Applies thirdparty/patches/*.patch to the rexglue-sdk submodule at configure
# time, so the submodule itself never needs to be edited or committed.
#
# Idempotent: a patch that is already applied (it reverse-applies cleanly) is
# skipped. A patch that neither applies nor reverse-applies is a hard error.
# Whitespace is ignored so the patches work with both LF and CRLF checkouts.

find_package(Git REQUIRED)

set(SDK_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/patches")
set(SDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk")

file(GLOB SDK_PATCHES "${SDK_PATCH_DIR}/*.patch")
list(SORT SDK_PATCHES)

foreach(patch IN LISTS SDK_PATCHES)
    get_filename_component(patch_name "${patch}" NAME)
    set(apply_args apply --ignore-whitespace "${patch}")

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --ignore-whitespace "${patch}"
        WORKING_DIRECTORY "${SDK_SOURCE_DIR}"
        RESULT_VARIABLE already_applied
        OUTPUT_QUIET ERROR_QUIET)
    if(already_applied EQUAL 0)
        message(STATUS "SDK patch already applied: ${patch_name}")
        continue()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${patch}"
        WORKING_DIRECTORY "${SDK_SOURCE_DIR}"
        RESULT_VARIABLE apply_result
        ERROR_VARIABLE apply_error)
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply SDK patch ${patch_name}:\n${apply_error}")
    endif()
    message(STATUS "SDK patch applied: ${patch_name}")
endforeach()
