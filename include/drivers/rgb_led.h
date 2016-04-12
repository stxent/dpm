/*
 * drivers/rgb_led.h
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_RGB_LED_H_
#define DPM_DRIVERS_RGB_LED_H_
/*----------------------------------------------------------------------------*/
#include <stdbool.h>
#include <entity.h>
#include <pwm.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const RgbLed;
/*----------------------------------------------------------------------------*/
struct RgbLedConfig
{
  /** Mandatory: red channel. */
  struct Pwm *red;
  /** Mandatory: green channel. */
  struct Pwm *green;
  /** Mandatory: blue channel. */
  struct Pwm *blue;
  /** Mandatory: resolution. */
  uint16_t resolution;
  /** Optional: signal inversion. */
  bool inversion;
};
/*----------------------------------------------------------------------------*/
struct RgbLed
{
  struct Entity base;

  /* Color channels */
  struct Pwm *channels[3];
  /* Signal resolution */
  uint16_t resolution;
  /* Enable signal inversion */
  bool inversion;
};
/*----------------------------------------------------------------------------*/
void rgbLedSet(struct RgbLed *, uint16_t, uint8_t, uint8_t);
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_RGB_LED_H_ */
