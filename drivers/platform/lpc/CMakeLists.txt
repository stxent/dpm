# Copyright (C) 2018 xent
# Project is distributed under the terms of the MIT License

if(${PLATFORM} MATCHES "LPC82X")
    set(HAS_NXP_GPTIMER FALSE)
    set(HAS_NXP_SSP FALSE)
    set(HAS_NXP_UART_V1 FALSE)
else()
    set(HAS_NXP_GPTIMER TRUE)
    set(HAS_NXP_SSP TRUE)
    set(HAS_NXP_UART_V1 TRUE)
endif()

if(${PLATFORM} MATCHES "LPC17XX|LPC43XX")
    set(HAS_NXP_GPDMA TRUE)
else()
    set(HAS_NXP_GPDMA FALSE)
endif()

if(${PLATFORM} MATCHES "LPC11EXX|LPC13UXX|LPC17XX|LPC43XX")
    set(HAS_NXP_IRDA_V1 TRUE)
else()
    set(HAS_NXP_IRDA_V1 FALSE)
endif()

if(${PLATFORM} MATCHES "LPC43XX")
    set(HAS_NXP_SGPIO TRUE)
else()
    set(HAS_NXP_SGPIO FALSE)
endif()

if(HAS_NXP_GPTIMER AND HAS_NXP_IRDA_V1)
    list(APPEND SOURCE_FILES "gen_1/irda.c")
    list(APPEND SOURCE_FILES "gen_1/irda_timer.c")
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
    list(APPEND SOURCE_FILES "one_wire_ssp.c")
    list(APPEND SOURCE_FILES "ws281x_ssp.c")
endif()

if(HAS_NXP_UART_V1)
    list(APPEND SOURCE_FILES "gen_1/one_wire_uart.c")
endif()

if(SOURCE_FILES)
    add_library(dpm_platform OBJECT ${SOURCE_FILES})
endif()
