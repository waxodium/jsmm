cmake_minimum_required(VERSION 3.20)

project(jsmm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")

file(GLOB_RECURSE JSMM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
)

add_executable(jsd ${JSMM_SOURCES})

target_include_directories(jsd PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(jsd SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
)

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(jsd PRIVATE ${LLVM_DEFINITIONS_LIST})

target_compile_options(jsd PRIVATE
    -Wall
    -Wextra
    -Werror
    -O2
)

find_library(LLVM_LIB NAMES LLVM-23 LLVM PATHS /usr/lib)
if(NOT LLVM_LIB)
    message(FATAL_ERROR "Could not find libLLVM (LLVM-23 / LLVM)")
endif()
target_link_libraries(jsd PRIVATE ${LLVM_LIB})

set_target_properties(jsd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
)
