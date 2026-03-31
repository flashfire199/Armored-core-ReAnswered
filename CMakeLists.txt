
# acre - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(acre LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(ACRE_SOURCES
    src/main.cpp
    src/camera_trace_patch.cpp
    src/rmb_patch.cpp
)

if(WIN32)
    add_executable(acre WIN32 ${ACRE_SOURCES})
else()
    add_executable(acre ${ACRE_SOURCES})
endif()

rexglue_setup_target(acre)
