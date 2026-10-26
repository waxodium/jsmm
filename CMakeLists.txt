cmake_minimum_required(VERSION 3.20)

project(jsmm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

file(GLOB_RECURSE JSMM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

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

target_link_libraries(jsd PRIVATE 
    LLVM
)

set_target_properties(jsd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
)

