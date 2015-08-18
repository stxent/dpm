/*
 * software_pwm.h
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef SOFTWARE_PWM_H_
#define SOFTWARE_PWM_H_
/*----------------------------------------------------------------------------*/
#include <stdbool.h>
#include <containers/list.h>
#include <entity.h>
#include <pwm.h>
#include <timer.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const SoftwarePwmUnit;
extern const struct PwmClass * const SoftwarePwm;
/*----------------------------------------------------------------------------*/
struct SoftwarePwmUnitConfig
{
  /** Mandatory: hardware timer. */
  struct Timer *timer;
  /** Mandatory: switching frequency. */
  uint32_t frequency;
  /** Mandatory: cycle resolution. */
  uint32_t resolution;
};
/*----------------------------------------------------------------------------*/
struct SoftwarePwmUnit
{
  struct Entity parent;

  /* Hardware timer */
  struct Timer *timer;
  /* Channels */
  struct List channels;
  /* Current value */
  uint32_t iteration;
  /* Unit resolution */
  uint32_t resolution;
};
/*----------------------------------------------------------------------------*/
struct SoftwarePwmConfig
{
  /** Mandatory: peripheral unit. */
  struct SoftwarePwmUnit *parent;
  /** Optional: initial duration in timer ticks. */
  uint32_t duration;
  /** Mandatory: pin used as an output for modulated signal. */
  pin_t pin;
};
/*----------------------------------------------------------------------------*/
struct SoftwarePwm
{
  struct Pwm parent;

  /* Pointer to a parent unit */
  struct SoftwarePwmUnit *unit;
  /* Pin descriptor */
  struct Pin pin;
  /* Current duration */
  uint32_t duration;
};
/*----------------------------------------------------------------------------*/
void *softwarePwmCreate(void *, pin_t, uint32_t);
/*----------------------------------------------------------------------------*/
#endif /* SOFTWARE_PWM_H_ */
