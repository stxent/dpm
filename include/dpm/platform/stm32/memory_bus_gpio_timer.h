/*
 * platform/stm32/memory_bus_gpio_timer.h
 * Copyright (C) 2019 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_PLATFORM_STM32_MEMORY_BUS_GPIO_TIMER_H_
#define DPM_PLATFORM_STM32_MEMORY_BUS_GPIO_TIMER_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/stm32/gptimer_base.h>
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

  /* Current timer frequency */
  uint32_t frequency;

  /* Output compare channel */
  uint8_t channel;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_PLATFORM_STM32_MEMORY_BUS_GPIO_TIMER_H_ */
