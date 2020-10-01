/*
 * drivers/button.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_BUTTON_H_
#define DPM_DRIVERS_BUTTON_H_
/*----------------------------------------------------------------------------*/
#include <halm/interrupt.h>
#include <halm/pin.h>
#include <halm/timer.h>
/*----------------------------------------------------------------------------*/
extern const struct InterruptClass * const Button;
/*----------------------------------------------------------------------------*/
struct ButtonConfig
{
  /** Mandatory: pin interrupt. */
  struct Interrupt *interrupt;
  /** Mandatory: tick timer. */
  struct Timer *timer;
  /** Mandatory: pin. */
  PinNumber pin;
  /** Optional: delay. */
  unsigned int delay;
  /** Mandatory: active level. */
  bool level;
};
/*----------------------------------------------------------------------------*/
struct Button
{
  struct Interrupt base;

  void (*callback)(void *);
  void *callbackArgument;

  struct Interrupt *interrupt;
  struct Timer *timer;
  struct Pin pin;

  unsigned int counter;
  unsigned int delay;
  bool level;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_BUTTON_H_ */
