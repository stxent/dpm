/*
 * rgb_led.c
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <dpm/drivers/rgb_led.h>
/*----------------------------------------------------------------------------*/
static enum Result ledInit(void *, const void *);
static void ledDeinit(void *);
/*----------------------------------------------------------------------------*/
const struct EntityClass * const RgbLed = &(const struct EntityClass){
    .size = sizeof(struct RgbLed),
    .init = ledInit,
    .deinit = ledDeinit
};
/*----------------------------------------------------------------------------*/
static enum Result ledInit(void *object, const void *configBase)
{
  const struct RgbLedConfig * const config = configBase;
  struct RgbLed * const led = object;

  led->channels[0] = config->red;
  led->channels[1] = config->green;
  led->channels[2] = config->blue;
  led->resolution = pwmGetResolution(led->channels[0]);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void ledDeinit(void *object __attribute__((unused)))
{
}
/*----------------------------------------------------------------------------*/
void rgbLedSet(struct RgbLed *led, uint16_t hue, uint8_t saturation,
    uint8_t value)
{
  static const uint8_t mapping[6][3] = {
      {0, 2, 1},
      {3, 0, 1},
      {1, 0, 2},
      {1, 3, 0},
      {2, 1, 0},
      {0, 1, 3}
  };

  const uint16_t intValue = value * 100;
  const uint16_t minValue = (100 - saturation) * value;
  const uint16_t delta = ((intValue - minValue) * (hue % 60)) / 60;

  const uint16_t fill[4] = {
      intValue,
      minValue,
      minValue + delta,
      intValue - delta
  };

  const uint8_t hi = (hue / 60) % 6;
  const uint32_t resolution = led->resolution;
  const uint32_t values[3] = {
      (fill[mapping[hi][0]] * resolution) / (100 * 100),
      (fill[mapping[hi][1]] * resolution) / (100 * 100),
      (fill[mapping[hi][2]] * resolution) / (100 * 100)
  };

  pwmSetDuration(led->channels[0], values[0]);
  pwmSetDuration(led->channels[1], values[1]);
  pwmSetDuration(led->channels[2], values[2]);
}
