/*
 * drivers/rgb_led.h
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DRIVERS_RGB_LED_H_
#define DPM_DRIVERS_RGB_LED_H_
/*----------------------------------------------------------------------------*/
#include <halm/pwm.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const RgbLed;

struct RgbLedConfig
{
  /** Mandatory: red channel. */
  struct Pwm *red;
  /** Mandatory: green channel. */
  struct Pwm *green;
  /** Mandatory: blue channel. */
  struct Pwm *blue;
};

struct RgbLed
{
  struct Entity base;

  /* Color channels */
  struct Pwm *channels[3];
  /* Signal resolution */
  uint16_t resolution;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void rgbLedSet(struct RgbLed *, uint16_t, uint8_t, uint8_t);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_RGB_LED_H_ */
