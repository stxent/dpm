# Copyright (C) 2018 xent
# Project is distributed under the terms of the GNU General Public License v3.0

project(dpm C)
cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0069 NEW)

option(USE_LTO "Enable Link Time Optimization." OFF)

# Default compiler flags
set(FLAGS_DEFAULT "-fdata-sections -ffunction-sections -Wall -Wextra -Wshadow -pedantic")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAGS_DEFAULT} ${FLAGS_CPU}")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

if(${USE_LTO})
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_C_COMPILE_OPTIONS_IPO -flto -ffat-lto-objects)
    endif()
endif()

set(DIRECTORY_LIST generic platform)
set(LIBRARY_OBJECTS "")
set(LIBRARY_TARGETS "")

add_subdirectory(drivers)

foreach(ENTRY ${DIRECTORY_LIST})
    if(TARGET dpm_${ENTRY})
        list(APPEND LIBRARY_TARGETS dpm_${ENTRY})
        list(APPEND LIBRARY_OBJECTS $<TARGET_OBJECTS:dpm_${ENTRY}>)
        target_include_directories(dpm_${ENTRY}
                PUBLIC "${PROJECT_SOURCE_DIR}/include"
        )
        target_link_libraries(dpm_${ENTRY}
                PUBLIC halm
        )
    endif()
endforeach()

add_library(${PROJECT_NAME} ${LIBRARY_OBJECTS})
target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBRARY_TARGETS})

install(TARGETS ${PROJECT_NAME} ARCHIVE DESTINATION lib)
