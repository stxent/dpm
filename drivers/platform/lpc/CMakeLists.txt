# Copyright (C) 2018 xent
# Project is distributed under the terms of the MIT License

set(HAS_NXP_GPTIMER TRUE)
set(HAS_NXP_SSP TRUE)

if(${PLATFORM} MATCHES "LPC17XX|LPC43XX")
    set(HAS_NXP_GPDMA TRUE)
else()
    set(HAS_NXP_GPDMA FALSE)
endif()

if(${PLATFORM} MATCHES "LPC11EXX|LPC13UXX|LPC17XX|LPC43XX")
    set(HAS_NXP_IRDA TRUE)
else()
    set(HAS_NXP_IRDA FALSE)
endif()

if(${PLATFORM} MATCHES "LPC43XX")
    set(HAS_NXP_SGPIO TRUE)
else()
    set(HAS_NXP_SGPIO FALSE)
endif()

if(HAS_NXP_GPTIMER AND HAS_NXP_IRDA)
    list(APPEND SOURCE_FILES "irda.c")
    list(APPEND SOURCE_FILES "irda_timer.c")
endif()

if(HAS_NXP_GPTIMER)
    list(APPEND SOURCE_FILES "memory_bus_gpio.c")
    list(APPEND SOURCE_FILES "memory_bus_gpio_timer.c")
endif()

if(HAS_NXP_GPTIMER AND HAS_NXP_GPDMA)
    list(APPEND SOURCE_FILES "memory_bus_dma.c")
    list(APPEND SOURCE_FILES "memory_bus_dma_finalizer.c")
    list(APPEND SOURCE_FILES "memory_bus_dma_timer.c")
endif()

if(HAS_NXP_SGPIO)
    list(APPEND SOURCE_FILES "sgpio_bus.c")
    list(APPEND SOURCE_FILES "sgpio_bus_dma.c")
    list(APPEND SOURCE_FILES "sgpio_bus_timer.c")
endif()

if(HAS_NXP_SSP)
    list(APPEND SOURCE_FILES "ws281x_ssp.c")
endif()

if(SOURCE_FILES)
    add_library(dpm_platform OBJECT ${SOURCE_FILES})
endif()
