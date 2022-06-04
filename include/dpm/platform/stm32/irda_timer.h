/*
 * platform/lpc/irda_timer.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_PLATFORM_LPC_IRDA_TIMER_H_
#define DPM_PLATFORM_LPC_IRDA_TIMER_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/lpc/gptimer_base.h>
/*----------------------------------------------------------------------------*/
extern const struct TimerClass * const IrdaTimer;

struct IrdaTimerConfig
{
  /** Start of frame callback. */
  void (*sofCallback)(void *);
  /** Start of data callback. */
  void (*dataCallback)(void *);
  /** Callback argument. */
  void *argument;
  /** Mandatory: timer frequency. */
  uint32_t frequency;
  /** Mandatory: slot time in timer ticks. */
  uint32_t period;
  /** Mandatory: break signal width in timer ticks. */
  uint32_t guard;
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

  void (*dataCallback)(void *);
  void (*sofCallback)(void *);
  void *callbackArgument;

  /* Break width */
  uint32_t guard;
  /* Start of data event */
  uint8_t dataChannel;
  /* Start of frame event */
  uint8_t syncChannel;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_PLATFORM_LPC_IRDA_TIMER_H_ */
