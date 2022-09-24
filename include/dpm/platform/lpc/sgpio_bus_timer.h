/*
 * platform/lpc/sgpio_bus_timer.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_PLATFORM_LPC_SGPIO_BUS_TIMER_H_
#define DPM_PLATFORM_LPC_SGPIO_BUS_TIMER_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/lpc/gptimer_base.h>
/*----------------------------------------------------------------------------*/
extern const struct TimerClass * const SgpioBusTimer;

struct SgpioBusTimerConfig
{
  /** Mandatory: peripheral identifier. */
  uint8_t channel;
  /** Mandatory: capture channel. */
  uint8_t capture;
  /** Mandatory: match channel. */
  uint8_t match;
};

struct SgpioBusTimer
{
  struct GpTimerBase base;

  /* Match event for request generation */
  uint8_t match;
  /* Counter reset event */
  uint8_t reset;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_PLATFORM_LPC_SGPIO_BUS_TIMER_H_ */
