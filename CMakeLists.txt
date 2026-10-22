cmake_minimum_required(VERSION 3.16)

project(jsmm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE JSMM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
)

add_executable(jsd ${JSMM_SOURCES})

target_include_directories(jsd PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(jsd PRIVATE
    -Wall
    -Wextra
    -Werror
    -O2
)

set_target_properties(jsd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
)

enable_testing()

set(JSMM_TEST_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/tests/run.sh")

file(GLOB JSMM_TEST_CASES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/*.js")
foreach(case_file ${JSMM_TEST_CASES})
    get_filename_component(case_name ${case_file} NAME_WE)
    add_test(NAME "case.${case_name}"
        COMMAND ${JSMM_TEST_RUNNER} cases ${case_name}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

file(GLOB JSMM_TEST_ERRORS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/errors/*.js")
foreach(case_file ${JSMM_TEST_ERRORS})
    get_filename_component(case_name ${case_file} NAME_WE)
    add_test(NAME "error.${case_name}"
        COMMAND ${JSMM_TEST_RUNNER} errors ${case_name}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

file(GLOB JSMM_TEST_RUNTIME CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/*.js")
foreach(case_file ${JSMM_TEST_RUNTIME})
    get_filename_component(case_name ${case_file} NAME_WE)
    add_test(NAME "runtime.${case_name}"
        COMMAND ${JSMM_TEST_RUNNER} runtime ${case_name}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
