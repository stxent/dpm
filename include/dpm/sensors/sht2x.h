/*
 * sensors/sht2x.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_SHT2X_H_
#define DPM_SENSORS_SHT2X_H_
/*----------------------------------------------------------------------------*/
#include <dpm/sensors/sensor.h>
#include <halm/pin.h>
/*----------------------------------------------------------------------------*/
extern const struct SensorClass * const SHT2X;
extern const struct SensorClass * const SHT2XThermometer;

struct Interface;
struct Timer;

enum SHT2XResolution
{
  SHT2X_RESOLUTION_DEFAULT,
  /** 8 bit of Relative Humidity and 12 bit of Temperature */
  SHT2X_RESOLUTION_8BIT,
  /** 10 bit of Relative Humidity and 13 bit of Temperature */
  SHT2X_RESOLUTION_10BIT,
  /** 11 bit of Relative Humidity and 11 bit of Temperature */
  SHT2X_RESOLUTION_11BIT,
  /** 12 bit of Relative Humidity and 14 bit of Temperature */
  SHT2X_RESOLUTION_12BIT
};

struct SHT2XConfig
{
  /** Mandatory: serial interface. */
  struct Interface *bus;
  /** Mandatory: event timer. */
  struct Timer *timer;

  /** Optional: resolution configuration. */
  enum SHT2XResolution resolution;
  /** Optional: sensor address. */
  uint32_t address;
  /** Optional: baud rate of the serial interface. */
  uint32_t rate;
};

struct SHT2XThermometer;

struct SHT2X
{
  struct Sensor base;

  void *callbackArgument;
  void (*onErrorCallback)(void *, enum SensorResult);
  void (*onResultCallback)(void *, const void *, size_t);
  void (*onUpdateCallback)(void *);

  /* Thermometer sensor proxy */
  struct SHT2XThermometer *thermometer;

  /* Sensor bus */
  struct Interface *bus;
  /* Timer for periodic events */
  struct Timer *timer;
  /* Bus address */
  uint32_t address;
  /* Baud rate of the serial interface */
  uint32_t rate;

  /* Raw pressure value */
  uint16_t humidity;
  /* Raw temperature value */
  uint16_t temperature;

  /* Buffer for received data */
  uint8_t buffer[2];
  /* Resolution settings */
  uint8_t resolution;
  /* Current operation */
  uint8_t state;
  /* Reset calibration data */
  bool reset;
  /* Start conversion */
  bool start;
  /* Stop after current operation */
  bool stop;
};

struct SHT2XThermometer
{
  struct Sensor base;

  void *callbackArgument;
  void (*onErrorCallback)(void *, enum SensorResult);
  void (*onResultCallback)(void *, const void *, size_t);
  void (*onUpdateCallback)(void *);

  bool enabled;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

struct SHT2XThermometer *sht2xMakeThermometer(struct SHT2X *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_SHT2X_H_ */
