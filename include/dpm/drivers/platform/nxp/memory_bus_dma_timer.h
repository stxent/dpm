/*
 * drivers/platform/nxp/memory_bus_dma_timer.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_TIMER_H_
#define DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_TIMER_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/nxp/gptimer_base.h>
/*----------------------------------------------------------------------------*/
extern const struct TimerClass * const MemoryBusDmaTimer;
/*----------------------------------------------------------------------------*/
struct MemoryBusDmaTimerConfig
{
  /** Optional: memory operation cycle in timer ticks. */
  uint32_t cycle;
  /** Optional: external clock input. */
  pinNumber input;
  /** Mandatory: pin used as output for memory control signal. */
  pinNumber leading;
  /** Mandatory: pin used as output for memory control signal. */
  pinNumber trailing;
  /** Optional: interrupt priority. */
  irqPriority priority;
  /** Mandatory: peripheral identifier. */
  uint8_t channel;
  /** Optional: selects control mode timings. */
  bool control;
  /** Optional: enables inversion of control signal. */
  bool inversion;
};
/*----------------------------------------------------------------------------*/
struct MemoryBusDmaTimer
{
  struct GpTimerBase parent;

  void (*callback)(void *);
  void *callbackArgument;

  uint32_t interruptValue;
  uint32_t matchValue;
  uint32_t offset;

  /* Callback event channel */
  uint8_t leadingChannel;
  /* Match result output channel */
  uint8_t trailingChannel;
  /* Counter reset event */
  uint8_t resetChannel;
  /* Select control mode */
  bool control;
  /* Primary channel type */
  bool leading;
};
/*----------------------------------------------------------------------------*/
uint8_t memoryBusDmaTimerPrimaryChannel(const struct MemoryBusDmaTimer *);
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_TIMER_H_ */
