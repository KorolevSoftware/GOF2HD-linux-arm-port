cmake_minimum_required(VERSION 3.18)
project(GOF2HD LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4
   OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|ARM)")
  message(FATAL_ERROR "GOF2HD requires 32-bit ARM Linux with a hardfp GCC toolchain")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(GOF_OUT "${PROJECT_SOURCE_DIR}/port/run-native" CACHE PATH "Runtime output directory")
set(GOF_SYS_LIB "/usr/lib32" CACHE PATH "Target SDL2 library directory")
set(GOF_CFLAGS "-O2 -g -fno-stack-protector -fno-builtin" CACHE STRING "Port compiler flags")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Runtime library search paths are provided by the launch scripts.
set(CMAKE_SKIP_RPATH TRUE)

add_subdirectory(port)
