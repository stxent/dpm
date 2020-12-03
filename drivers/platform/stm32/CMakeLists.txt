# Copyright (C) 2019 xent
# Project is distributed under the terms of the GNU General Public License v3.0

set(HAS_STM32_GPTIMER TRUE)

list(APPEND SOURCE_FILES "irda.c")

if(${HAS_STM32_GPTIMER})
    list(APPEND SOURCE_FILES "memory_bus_gpio.c")
    list(APPEND SOURCE_FILES "memory_bus_gpio_timer.c")
endif()

if(SOURCE_FILES)
    add_library(dpm_platform OBJECT ${SOURCE_FILES})
endif()
