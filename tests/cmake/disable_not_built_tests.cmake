set(_legend2_tests_dir)
if(DEFINED CMAKE_PARENT_LIST_FILE AND NOT CMAKE_PARENT_LIST_FILE STREQUAL "")
  get_filename_component(_legend2_tests_dir "${CMAKE_PARENT_LIST_FILE}" DIRECTORY)
endif()

if(NOT _legend2_tests_dir OR NOT EXISTS "${_legend2_tests_dir}")
  return()
endif()

file(GLOB _legend2_include_files "${_legend2_tests_dir}/*_include.cmake")
set(_legend2_not_built_tests)
foreach(_legend2_include_file IN LISTS _legend2_include_files)
  file(STRINGS
       "${_legend2_include_file}"
       _legend2_add_test_lines
       REGEX "^[ \t]*add_test\\([A-Za-z0-9_]+_NOT_BUILT[ \t].*\\)$")
  foreach(_legend2_add_test_line IN LISTS _legend2_add_test_lines)
    string(
        REGEX REPLACE
        "^[ \t]*add_test\\(([A-Za-z0-9_]+_NOT_BUILT)[ \t].*\\)$"
        "\\1"
        _legend2_test_name
        "${_legend2_add_test_line}")
    list(APPEND _legend2_not_built_tests "${_legend2_test_name}")
  endforeach()
endforeach()

list(REMOVE_DUPLICATES _legend2_not_built_tests)
foreach(_legend2_test_name IN LISTS _legend2_not_built_tests)
  set_tests_properties(
      "${_legend2_test_name}"
      PROPERTIES
        DISABLED TRUE
        LABELS "not_built")
endforeach()
