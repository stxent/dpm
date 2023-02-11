/*
 * rgb_led.h
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_RGB_LED_H_
#define DPM_RGB_LED_H_
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
  /** Mandatory: PWM resolution. */
  uint32_t resolution;
};

struct RgbLed
{
  struct Entity base;

  /* Color channels */
  struct Pwm *channels[3];
  /* PWM resolution */
  uint32_t resolution;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void rgbLedSetHSV(struct RgbLed *, uint16_t, uint8_t, uint8_t);
void rgbLedSetRGB(struct RgbLed *, uint8_t, uint8_t, uint8_t);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_RGB_LED_H_ */
