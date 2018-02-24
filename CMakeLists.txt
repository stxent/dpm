# Copyright (C) 2018 xent
# Project is distributed under the terms of the GNU General Public License v3.0

project(dpm C)
cmake_minimum_required(VERSION 3.6)
cmake_policy(SET CMP0054 NEW)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build: Debug Release." FORCE)
endif()

option(USE_LTO "Enable Link Time Optimization." OFF)

set(PATH_XCORE "${PROJECT_SOURCE_DIR}/../xcore" CACHE STRING "Path to the XCORE library.")
set(PATH_HALM "${PROJECT_SOURCE_DIR}/../halm" CACHE STRING "Path to the HALM library.")

# Default compiler flags
set(FLAGS_DEFAULT "-fdata-sections -ffunction-sections -Wall -Wextra -Wshadow -pedantic")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAGS_DEFAULT} ${FLAGS_CPU}")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

if(${USE_LTO})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto -ffat-lto-objects")
endif()

if(DEFINED PLATFORM)
    add_definitions(-D${PLATFORM})
else()
    if(UNIX)
        add_definitions(-D_POSIX_C_SOURCE=200809L)
    endif()
endif()

include_directories("${PATH_XCORE}/include")
include_directories("${PATH_HALM}/include")
include_directories("${PROJECT_SOURCE_DIR}/include")

# Gather files for the library
add_subdirectory(drivers)
add_library(${PROJECT_NAME} $<TARGET_OBJECTS:drivers>)
install(TARGETS ${PROJECT_NAME} ARCHIVE DESTINATION lib)
