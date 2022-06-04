/*
 * platform/lpc/memory_bus_dma_timer.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_PLATFORM_LPC_MEMORY_BUS_DMA_TIMER_H_
#define DPM_PLATFORM_LPC_MEMORY_BUS_DMA_TIMER_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/lpc/gptimer_base.h>
/*----------------------------------------------------------------------------*/
extern const struct TimerClass * const MemoryBusDmaClock;
extern const struct TimerClass * const MemoryBusDmaControl;

struct MemoryBusDmaClockConfig
{
  /** Mandatory: memory operation cycle in timer ticks. */
  uint32_t cycle;
  /** Mandatory: pin used as output for memory control signal. */
  PinNumber leading;
  /** Mandatory: pin used as output for memory control signal. */
  PinNumber trailing;
  /** Optional: interrupt priority. */
  IrqPriority priority;
  /** Mandatory: peripheral identifier. */
  uint8_t channel;
  /** Optional: enables inversion of control signal. */
  bool inversion;
};

struct MemoryBusDmaControlConfig
{
  /** Mandatory: external clock input. */
  PinNumber input;
  /** Mandatory: pin used as output for memory control signal. */
  PinNumber leading;
  /** Mandatory: pin used as output for memory control signal. */
  PinNumber trailing;
  /** Mandatory: peripheral identifier. */
  uint8_t channel;
  /** Optional: enables inversion of control signal. */
  bool inversion;
};

struct MemoryBusDmaTimer
{
  struct GpTimerBase base;

  void (*callback)(void *);
  void *callbackArgument;

  uint32_t match;

  /* Match event for a leading edge of a signal */
  uint8_t leading;
  /* Match event for a trailing edge of a signal */
  uint8_t trailing;
  /* Counter reset event */
  uint8_t reset;
  /* Match event for a chip select signal */
  uint8_t select;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_PLATFORM_LPC_MEMORY_BUS_DMA_TIMER_H_ */
