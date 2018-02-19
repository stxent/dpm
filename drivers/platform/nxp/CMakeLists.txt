# Copyright (C) 2018 xent
# Project is distributed under the terms of the GNU General Public License v3.0

string(FIND "${PLATFORM}" "LPC" PLATFORM_IS_NXP)
if(${PLATFORM_IS_NXP} EQUAL 0)
    set(HAS_NXP_GPTIMER TRUE)
endif()

if("${PLATFORM}" STREQUAL "LPC17XX" OR "${PLATFORM}" STREQUAL "LPC43XX")
    set(HAS_NXP_GPDMA TRUE)
endif()

if(${HAS_NXP_GPTIMER})
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_LIST_DIR}/memory_bus_gpio.c")
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_LIST_DIR}/memory_bus_gpio_timer.c")
endif()

if(${HAS_NXP_GPTIMER} AND ${HAS_NXP_GPDMA})
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_LIST_DIR}/memory_bus_dma.c")
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_LIST_DIR}/memory_bus_dma_finalizer.c")
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_LIST_DIR}/memory_bus_dma_timer.c")
endif()
