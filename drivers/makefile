#Copyright (C) 2015 xent
#Project is distributed under the terms of the GNU General Public License v3.0

ifeq ($(CONFIG_DRIVERS_DS18B20),y)
  CSOURCES += drivers/ds18b20.c
endif

ifeq ($(CONFIG_DRIVERS_RGB_LED),y)
  CSOURCES += drivers/rgb_led.c
endif

ifeq ($(CONFIG_DRIVERS_SOFTWARE_PWM),y)
  CSOURCES += drivers/software_pwm.c
endif

ifeq ($(CONFIG_DRIVERS_USB_MOUSE),y)
  CSOURCES += drivers/usb/mouse.c
endif

ifeq ($(CONFIG_DRIVERS_DISPLAYS_HD44780),y)
  CSOURCES += drivers/displays/hd44780.c
endif

ifeq ($(CONFIG_DRIVERS_DISPLAYS_S6D1121),y)
  CSOURCES += drivers/displays/s6d1121.c
endif

ifeq ($(CONFIG_DRIVERS_DISPLAYS_ST7735),y)
  CSOURCES += drivers/displays/st7735.c
endif

ifeq ($(CONFIG_DRIVERS_PLATFORM_NXP_MEMORY_BUS_GPIO),y)
  CSOURCES += drivers/platform/nxp/memory_bus_gpio.c
  CSOURCES += drivers/platform/nxp/memory_bus_gpio_timer.c
endif

ifeq ($(CONFIG_DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA),y)
  CSOURCES += drivers/platform/nxp/memory_bus_dma.c
  CSOURCES += drivers/platform/nxp/memory_bus_dma_finalizer.c
  CSOURCES += drivers/platform/nxp/memory_bus_dma_timer.c
endif
