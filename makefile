#Copyright (C) 2015 xent
#Project is distributed under the terms of the GNU General Public License v3.0

PROJECT = dpm
PROJECTDIR = $(shell pwd)

CONFIG_FILE ?= .config
CROSS_COMPILE ?= arm-none-eabi-

#External libraries
XCORE_PATH ?= $(PROJECTDIR)/../xcore
HALM_PATH ?= $(PROJECTDIR)/../halm

-include $(CONFIG_FILE)

AR = $(CROSS_COMPILE)ar
CC = $(CROSS_COMPILE)gcc

ifeq ($(CONFIG_CPU_FAMILY),"LPC11XX")
  CORE := m0
  CORE_TYPE := cortex
  PLATFORM := lpc11xx
  PLATFORM_TYPE := nxp

  #Platform-specific options
  CPU_FLAGS := -mcpu=cortex-m0 -mthumb
else ifeq ($(CONFIG_CPU_FAMILY),"LPC11EXX")
  CORE := m0
  CORE_TYPE := cortex
  PLATFORM := lpc11exx
  PLATFORM_TYPE := nxp

  #Platform-specific options
  CPU_FLAGS := -mcpu=cortex-m0 -mthumb
else ifeq ($(CONFIG_CPU_FAMILY),"LPC13XX")
  CORE := m3
  CORE_TYPE := cortex
  PLATFORM := lpc13xx
  PLATFORM_TYPE = nxp

  #Platform-specific options
  CPU_FLAGS := -mcpu=cortex-m3 -mthumb
else ifeq ($(CONFIG_CPU_FAMILY),"LPC17XX")
  CORE := m3
  CORE_TYPE := cortex
  PLATFORM := lpc17xx
  PLATFORM_TYPE := nxp

  #Platform-specific options
  CPU_FLAGS := -mcpu=cortex-m3 -mthumb
else ifeq ($(CONFIG_CPU_FAMILY),"LPC43XX")
  CORE := m4
  CORE_TYPE := cortex
  PLATFORM := lpc43xx
  PLATFORM_TYPE := nxp

  #Platform-specific options
  CPU_FLAGS := -mcpu=cortex-m4 -mthumb
else
  ifneq ($(MAKECMDGOALS),menuconfig)
    $(error Target architecture is undefined)
  endif
endif

ifeq ($(CONFIG_OPTIMIZATIONS),"full")
  OPT_FLAGS += -O3 -DNDEBUG
else ifeq ($(CONFIG_OPTIMIZATIONS),"size")
  OPT_FLAGS += -Os -DNDEBUG
else ifeq ($(CONFIG_OPTIMIZATIONS),"none")
  OPT_FLAGS += -O0 -g3
else
  OPT_FLAGS += $(CONFIG_OPTIMIZATIONS)
endif

#Configure common paths and libraries
INCLUDEPATH += -Iinclude -I"$(XCORE_PATH)/include" -I"$(HALM_PATH)/include"
OUTPUTDIR = build_$(PLATFORM)

#Configure compiler options
CFLAGS += -std=c11 -Wall -Wextra -Winline -pedantic -Wshadow
CFLAGS += -fmessage-length=0 -fno-builtin -ffunction-sections -fdata-sections
CFLAGS += $(CPU_FLAGS) $(CONFIG_FLAGS) $(OPT_FLAGS)
CFLAGS += -D$(shell echo $(PLATFORM) | tr a-z A-Z)

#Modules specific for selected platform
include drivers/makefile

#Setup build flags
define append-flag
  ifeq ($$($(1)),y)
    CONFIG_FLAGS += -D$(1)
  else ifneq ($$($(1)),)
    CONFIG_FLAGS += -D$(1)=$$($(1))
  endif
endef

$(foreach entry,$(FLAG_NAMES),$(eval $(call append-flag,$(entry))))

#Configure targets
PROJECT_FILE += $(OUTPUTDIR)/lib$(PROJECT).a

TARGETS += $(PROJECT_FILE)
COBJECTS = $(CSOURCES:%.c=$(OUTPUTDIR)/%.o)

.PHONY: all clean menuconfig
.SUFFIXES:
.DEFAULT_GOAL=all

all: $(TARGETS)

$(PROJECT_FILE): $(COBJECTS)
	$(AR) -r $@ $^

$(OUTPUTDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(INCLUDEPATH) -MMD -MF $(@:%.o=%.d) -MT $@ $< -o $@

clean:
	rm -f $(COBJECTS:%.o=%.d) $(COBJECTS)
	rm -f $(TARGETS)

menuconfig:
	kconfig-mconf kconfig

ifneq ($(MAKECMDGOALS),clean)
  -include $(COBJECTS:%.o=%.d)
endif
