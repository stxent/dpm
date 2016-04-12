/*
 * drivers/platform/nxp/memory_bus_gpio_timer.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_GPIO_TIMER_H_
#define DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_GPIO_TIMER_H_
/*----------------------------------------------------------------------------*/
#include <platform/nxp/gptimer_base.h>
/*----------------------------------------------------------------------------*/
extern const struct TimerClass *MemoryBusGpioTimer;
/*----------------------------------------------------------------------------*/
struct MemoryBusGpioTimerConfig
{
  /** Mandatory: timer frequency. */
  uint32_t frequency;
  /** Mandatory: period of a memory cycle in timer ticks. */
  uint32_t cycle;
  /** Mandatory: pin used as output for memory control signal. */
  pinNumber pin;
  /** Optional: timer interrupt priority. */
  irqPriority priority;
  /** Mandatory: timer block. */
  uint8_t channel;
  /** Mandatory: enables inversion of the control signal. */
  bool inversion;
};
/*----------------------------------------------------------------------------*/
struct MemoryBusGpioTimer
{
  struct GpTimerBase parent;

  void (*callback)(void *);
  void *callbackArgument;

  /* Callback event channel */
  uint8_t callbackChannel;
  /* Match result output channel */
  uint8_t eventChannel;
  /* Counter reset event */
  uint8_t resetChannel;
  /* Memory control signal inversion */
  bool inversion;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_GPIO_TIMER_H_ */
