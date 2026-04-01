
# acre - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(acre LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SSSE3 is required by SDK library sources on x86_64 (_mm_shuffle_epi8).
# Must be set before include() so it propagates into the SDK subdirectory.
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    add_compile_options(-mssse3)
endif()

include(generated/rexglue.cmake)

# Sources
set(ACRE_SOURCES
    src/main.cpp
    src/rmb_patch.cpp
)

if(WIN32)
    add_executable(acre WIN32 ${ACRE_SOURCES} src/camera_trace_patch.cpp)
else()
    add_executable(acre ${ACRE_SOURCES})
endif()

rexglue_setup_target(acre)
