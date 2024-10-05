/*
 * sensors/xpt2046.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_XPT2046_H_
#define DPM_SENSORS_XPT2046_H_
/*----------------------------------------------------------------------------*/
#include <dpm/sensors/sensor.h>
#include <halm/pin.h>
#include <stdbool.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
extern const struct SensorClass * const XPT2046;

struct Interface;
struct Interrupt;
struct Timer;

struct XPT2046Config
{
  /** Mandatory: serial interface. */
  void *bus;
  /** Mandatory: external interrupt. */
  void *event;
  /** Mandatory: event timer. */
  void *timer;

  /** Optional: bit rate of the serial interface. */
  uint32_t rate;
  /** Mandatory: pin used as Chip Select output. */
  PinNumber cs;

  /** Optional: touch threshold */
  uint16_t threshold;
  /** Mandatory: X resolution. */
  uint16_t x;
  /** Mandatory: Y resolution. */
  uint16_t y;
};

struct XPT2046
{
  struct Sensor base;

  void *callbackArgument;
  void (*onErrorCallback)(void *, enum SensorResult);
  void (*onResultCallback)(void *, const void *, size_t);
  void (*onUpdateCallback)(void *);

  /* Serial interface */
  struct Interface *bus;
  /* External event */
  struct Interrupt *event;
  /* Event timer */
  struct Timer *timer;
  /* Chip select */
  struct Pin cs;
  /* Baud rate of the serial interface */
  uint32_t rate;

  /* Response buffer */
  uint8_t rxBuffer[11];
  /* Command and status flags */
  uint8_t flags;
  /* Command buffer */
  uint8_t txBuffer[11];
  /* Current state */
  uint8_t state;

  uint16_t threshold;
  uint16_t xMax;
  uint16_t xMin;
  uint16_t xRes;
  uint16_t yMax;
  uint16_t yMin;
  uint16_t yRes;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void xpt2046ResetCalibration(struct XPT2046 *);
void xpt2046SetCalibration(struct XPT2046 *,
    uint16_t, uint16_t, uint16_t, uint16_t);
void xpt2046SetSensitivity(struct XPT2046 *, uint16_t);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_XPT2046_H_ */
