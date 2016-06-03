/*
 * drivers/software_pwm.h
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_SOFTWARE_PWM_H_
#define DPM_DRIVERS_SOFTWARE_PWM_H_
/*----------------------------------------------------------------------------*/
#include <stdbool.h>
#include <halm/pwm.h>
#include <halm/timer.h>
#include <xcore/containers/list.h>
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
  struct Entity base;

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
  /** Mandatory: pin used as an output for modulated signal. */
  pinNumber pin;
};
/*----------------------------------------------------------------------------*/
struct SoftwarePwm
{
  struct Pwm base;

  /* Pointer to a parent unit */
  struct SoftwarePwmUnit *unit;
  /* Current duration */
  uint32_t duration;
  /* Pin descriptor */
  struct Pin pin;
  /* Enables generation  of the signal */
  bool enabled;
};
/*----------------------------------------------------------------------------*/
void *softwarePwmCreate(void *, pinNumber);
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_SOFTWARE_PWM_H_ */
