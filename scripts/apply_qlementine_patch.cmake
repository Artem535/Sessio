set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH ROOT_DIR)

set(SUBMODULE_DIR "${ROOT_DIR}/third_party/qlementine")
set(PATCH_FILES
  "${ROOT_DIR}/patches/qlementine/0001-guard-widget-animation-manager-on-destroy.patch"
  "${ROOT_DIR}/patches/qlementine/0002-avoid-lazy-popup-in-child-event.patch"
  "${ROOT_DIR}/patches/qlementine/0003-size-combo-popup-through-stylesheet-wrapper.patch")

if(NOT EXISTS "${SUBMODULE_DIR}/.git")
  message(FATAL_ERROR "qlementine submodule is missing: ${SUBMODULE_DIR}")
endif()

function(apply_qlementine_patch PATCH_FILE)
execute_process(
  COMMAND git -C "${SUBMODULE_DIR}" apply --check "${PATCH_FILE}"
  RESULT_VARIABLE PATCH_CHECK_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)

if(PATCH_CHECK_RESULT EQUAL 0)
  execute_process(
    COMMAND git -C "${SUBMODULE_DIR}" apply "${PATCH_FILE}"
    RESULT_VARIABLE PATCH_APPLY_RESULT
    OUTPUT_VARIABLE PATCH_APPLY_OUTPUT
    ERROR_VARIABLE PATCH_APPLY_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT PATCH_APPLY_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to apply patch.\nstdout: ${PATCH_APPLY_OUTPUT}\nstderr: ${PATCH_APPLY_ERROR}")
  endif()
  message(STATUS "Applied qlementine patch")
  return()
endif()

execute_process(
  COMMAND git -C "${SUBMODULE_DIR}" apply --reverse --check "${PATCH_FILE}"
  RESULT_VARIABLE PATCH_REVERSE_CHECK_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)

if(PATCH_REVERSE_CHECK_RESULT EQUAL 0)
  message(STATUS "Patch already applied")
  return()
endif()

message(FATAL_ERROR "Patch cannot be applied cleanly: ${PATCH_FILE}")
endfunction()

foreach(PATCH_FILE IN LISTS PATCH_FILES)
  apply_qlementine_patch("${PATCH_FILE}")
endforeach()
