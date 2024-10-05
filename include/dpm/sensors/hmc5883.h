/*
 * sensors/hmc5883.h
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_HMC5883_H_
#define DPM_SENSORS_HMC5883_H_
/*----------------------------------------------------------------------------*/
#include <dpm/sensors/sensor.h>
#include <halm/pin.h>
/*----------------------------------------------------------------------------*/
extern const struct SensorClass * const HMC5883;

struct Interface;
struct Timer;

enum [[gnu::packed]] HMC5883Frequency
{
  HMC5883_FREQUENCY_DEFAULT,
  HMC5883_FREQUENCY_0_75HZ,
  HMC5883_FREQUENCY_1_5HZ,
  HMC5883_FREQUENCY_3_HZ,
  HMC5883_FREQUENCY_7_5HZ,
  HMC5883_FREQUENCY_15HZ,
  HMC5883_FREQUENCY_30HZ,
  HMC5883_FREQUENCY_75HZ,

  HMC5883_FREQUENCY_END
};

enum [[gnu::packed]] HMC5883Gain
{
  HMC5883_GAIN_DEFAULT,
  HMC5883_GAIN_880MGA,
  HMC5883_GAIN_1300MGA,
  HMC5883_GAIN_1900MGA,
  HMC5883_GAIN_2500MGA,
  HMC5883_GAIN_4000MGA,
  HMC5883_GAIN_4700MGA,
  HMC5883_GAIN_5600MGA,
  HMC5883_GAIN_8100MGA,

  HMC5883_GAIN_END
};

enum [[gnu::packed]] HMC5883Oversampling
{
  HMC5883_OVERSAMPLING_DEFAULT,
  HMC5883_OVERSAMPLING_NONE,
  HMC5883_OVERSAMPLING_2,
  HMC5883_OVERSAMPLING_4,
  HMC5883_OVERSAMPLING_8,

  HMC5883_OVERSAMPLING_END
};

struct HMC5883Config
{
  /** Mandatory: serial interface. */
  void *bus;
  /** Mandatory: external interrupt. */
  void *event;
  /** Mandatory: event timer. */
  void *timer;

  /** Optional: sensor address. */
  uint32_t address;
  /** Optional: bit rate of the serial interface. */
  uint32_t rate;

  /** Mandatory: sample rate for the magnetometer. */
  enum HMC5883Frequency frequency;
  /** Optional: gain configuration. */
  enum HMC5883Gain gain;
  /** Optional: oversampling configuration. */
  enum HMC5883Oversampling oversampling;
};

struct HMC5883
{
  struct Sensor base;

  void *callbackArgument;
  void (*onErrorCallback)(void *, enum SensorResult);
  void (*onResultCallback)(void *, const void *, size_t);
  void (*onUpdateCallback)(void *);

  /* Sensor bus */
  struct Interface *bus;
  /* External event */
  struct Interrupt *event;
  /* Timer for periodic events */
  struct Timer *timer;
  /* Bus address */
  uint32_t address;
  /* Baud rate of the serial interface */
  uint32_t rate;

  /* Buffer for received data */
  uint8_t buffer[7];
  /* Calibration mode */
  uint8_t calibration;
  /* Command and status flags */
  uint8_t flags;
  /* Sample rate settings */
  uint8_t frequency;
  /* Gain settings */
  uint8_t gain;
  /* Oversampling settings */
  uint8_t oversampling;
  /* Current operation */
  uint8_t state;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void hmc5883ApplyNegOffset(struct HMC5883 *);
void hmc5883ApplyPosOffset(struct HMC5883 *);
void hmc5883EnableNormalMode(struct HMC5883 *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_HMC5883_H_ */
