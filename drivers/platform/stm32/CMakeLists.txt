# Copyright (C) 2019 xent
# Project is distributed under the terms of the MIT License

set(HAS_STM32_GPTIMER TRUE)

if(${PLATFORM} STREQUAL "STM32F1XX")
    list(APPEND SOURCE_FILES "gen_1/irda.c")
endif()

if(HAS_STM32_GPTIMER)
    list(APPEND SOURCE_FILES "memory_bus_gpio.c")
    list(APPEND SOURCE_FILES "memory_bus_gpio_timer.c")
endif()

if(SOURCE_FILES)
    add_library(dpm_platform OBJECT ${SOURCE_FILES})
endif()
