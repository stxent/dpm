/*
 * drivers/platform/lpc/memory_bus_gpio_timer.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_GPIO_TIMER_H_
#define DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_GPIO_TIMER_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/lpc/gptimer_base.h>
/*----------------------------------------------------------------------------*/
extern const struct TimerClass * const MemoryBusGpioTimer;

struct MemoryBusGpioTimerConfig
{
  /** Mandatory: timer frequency. */
  uint32_t frequency;
  /** Mandatory: period of a memory cycle in timer ticks. */
  uint32_t cycle;
  /** Mandatory: pin used as output for memory control signal. */
  PinNumber pin;
  /** Optional: timer interrupt priority. */
  IrqPriority priority;
  /** Mandatory: timer block. */
  uint8_t channel;
  /** Mandatory: enables inversion of the control signal. */
  bool inversion;
};

struct MemoryBusGpioTimer
{
  struct GpTimerBase base;

  void (*callback)(void *);
  void *callbackArgument;

  /* Callback event channel */
  uint8_t leading;
  /* Clock channel */
  uint8_t trailing;
  /* Counter reset event */
  uint8_t reset;
  /* Clock inversion */
  bool inversion;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_GPIO_TIMER_H_ */
