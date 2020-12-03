/*
 * drivers/platform/stm32/memory_bus_gpio.h
 * Copyright (C) 2019 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_PLATFORM_STM32_MEMORY_BUS_GPIO_H_
#define DPM_DRIVERS_PLATFORM_STM32_MEMORY_BUS_GPIO_H_
/*----------------------------------------------------------------------------*/
#include <halm/gpio_bus.h>
#include <halm/irq.h>
#include <halm/timer.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass *MemoryBusGpio;

struct MemoryBusGpioConfig
{
  /** Mandatory: external bus interface. */
  struct GpioBus *bus;
  /** Mandatory: period of a memory cycle in timer ticks. */
  uint32_t cycle;
  /** Mandatory: timer frequency. */
  uint32_t frequency;
  /** Mandatory: memory control signal. */
  PinNumber strobe;
  /** Optional: timer interrupt priority. */
  IrqPriority priority;
  /** Mandatory: timer block. */
  uint8_t timer;
  /** Mandatory: enables inversion of control signal. */
  bool inversion;
};

struct MemoryBusGpio
{
  struct Interface base;

  void (*callback)(void *);
  void *callbackArgument;

  struct GpioBus *bus;
  struct Timer *timer;

  /* Pointer to a buffer to be transmitted */
  const uint8_t *buffer;
  /* Number of bytes to be transmitted */
  uint32_t left;
  /* Transmission is currently active */
  bool active;
  /* Selection between blocking mode and zero copy mode */
  bool blocking;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_STM32_MEMORY_BUS_GPIO_H_ */
