# Copyright (C) 2018 xent
# Project is distributed under the terms of the GNU General Public License v3.0

if(DEFINED PLATFORM)
    string(FIND ${PLATFORM} "LPC" PLATFORM_IS_LPC)
    string(FIND ${PLATFORM} "STM32" PLATFORM_IS_STM32)

    if(${PLATFORM_IS_LPC} EQUAL 0)
        add_subdirectory(lpc)
    elseif(${PLATFORM_IS_STM32} EQUAL 0)
        add_subdirectory(stm32)
    endif()
endif()
