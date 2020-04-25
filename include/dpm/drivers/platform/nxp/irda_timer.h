/*
 * drivers/platform/nxp/irda_timer.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_PLATFORM_NXP_IRDA_TIMER_H_
#define DPM_DRIVERS_PLATFORM_NXP_IRDA_TIMER_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/nxp/gptimer_base.h>
/*----------------------------------------------------------------------------*/
extern const struct TimerClass * const IrdaTimer;

enum IrdaTimerEventType
{
  IRDA_TIMER_SYNC,
  IRDA_TIMER_DATA
};

struct IrdaTimerEvent
{
  void *argument;
  enum IrdaTimerEventType type;
};

struct IrdaTimerConfig
{
  /** Mandatory: timer frequency. */
  uint32_t frequency;
  /** Mandatory: slot time in timer ticks. */
  uint32_t period;
  /** Mandatory: sync signal width in timer ticks. */
  uint32_t sync;
  /** Optional: timer interrupt priority. */
  IrqPriority priority;
  /** Mandatory: timer block. */
  uint8_t channel;
  /** Mandatory: enable master mode. */
  bool master;
};

struct IrdaTimer
{
  struct GpTimerBase base;

  void (*callback)(void *);
  void *callbackArgument;

  /* Break width */
  uint32_t sync;
  /* Start of frame event */
  uint8_t syncChannel;
  /* Start of data event */
  uint8_t dataChannel;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_NXP_IRDA_TIMER_H_ */
