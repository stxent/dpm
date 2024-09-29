/*
 * button.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_BUTTON_H_
#define DPM_BUTTON_H_
/*----------------------------------------------------------------------------*/
#include <halm/interrupt.h>
#include <halm/pin.h>
/*----------------------------------------------------------------------------*/
extern const struct InterruptClass * const Button;

struct Timer;

struct ButtonConfig
{
  /** Mandatory: pin interrupt. */
  struct Interrupt *interrupt;
  /** Mandatory: tick timer, timer will be configured for 100 Hz tick rate. */
  struct Timer *timer;
  /** Mandatory: input pin. */
  PinNumber pin;
  /** Optional: debouncing delay in timer ticks. */
  unsigned short delay;
  /** Mandatory: active level. */
  bool level;
};

struct Button
{
  struct Interrupt base;

  void (*callback)(void *);
  void *callbackArgument;

  struct Interrupt *interrupt;
  struct Timer *timer;
  struct Pin pin;

  unsigned short counter;
  unsigned short delay;
  bool level;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_BUTTON_H_ */
