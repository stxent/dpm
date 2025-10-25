# Copyright (C) 2018 xent
# Project is distributed under the terms of the MIT License

cmake_minimum_required(VERSION 3.21)
project(dpm C)

find_package(halm QUIET)
if(halm_FOUND)
    message(STATUS "Found halm package")
endif()

# Default compiler flags

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_C_STANDARD 23)
endif()

if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_C_COMPILE_OPTIONS_IPO -flto=auto -ffat-lto-objects)
    endif()
endif()

# Collect files for object libraries

set(DIRECTORY_LIST audio displays generic gnss memory platform radio sensors usb)
set(LIBRARY_OBJECTS "")
set(LIBRARY_TARGETS "")

add_subdirectory(drivers)

foreach(ENTRY ${DIRECTORY_LIST})
    if(TARGET dpm_${ENTRY})
        list(APPEND LIBRARY_TARGETS dpm_${ENTRY})
        list(APPEND LIBRARY_OBJECTS $<TARGET_OBJECTS:dpm_${ENTRY}>)

        target_include_directories(dpm_${ENTRY} PUBLIC
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
        )

        target_link_libraries(dpm_${ENTRY}
                PUBLIC halm
        )

        install(TARGETS dpm_${ENTRY} EXPORT dpm-targets)
    endif()
endforeach()

add_library(${PROJECT_NAME} ${LIBRARY_OBJECTS})
target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBRARY_TARGETS})

# Configure library installation

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/dpm
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
)
install(TARGETS ${PROJECT_NAME}
        EXPORT dpm-targets
        ARCHIVE DESTINATION lib
        INCLUDES DESTINATION include
)
install(EXPORT dpm-targets
        FILE dpm-targets.cmake
        DESTINATION lib/dpm
)
install(FILES "${PROJECT_SOURCE_DIR}/config.cmake.in"
        DESTINATION lib/dpm
        RENAME dpm-config.cmake
)
