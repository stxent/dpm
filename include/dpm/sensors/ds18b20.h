/*
 * sensors/ds18b20.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_DS18B20_H_
#define DPM_SENSORS_DS18B20_H_
/*----------------------------------------------------------------------------*/
#include <dpm/sensors/sensor.h>
#include <stdbool.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
extern const struct SensorClass * const DS18B20;

struct Interface;
struct Timer;

enum DS18B20Resolution
{
  DS18B20_RESOLUTION_DEFAULT,
  DS18B20_RESOLUTION_9BIT,
  DS18B20_RESOLUTION_10BIT,
  DS18B20_RESOLUTION_11BIT,
  DS18B20_RESOLUTION_12BIT
};

struct DS18B20Config
{
  /** Mandatory: sensor bus. */
  void *bus;
  /* Timer for timeout calculations */
  void *timer;
  /** Optional: device address. */
  uint64_t address;
  /** Optional: temperature resolution. */
  enum DS18B20Resolution resolution;
};

struct DS18B20
{
  struct Sensor base;

  void *callbackArgument;
  void (*onErrorCallback)(void *, enum SensorResult);
  void (*onResultCallback)(void *, const void *, size_t);
  void (*onUpdateCallback)(void *);

  /* Sensor bus */
  struct Interface *bus;
  /* Timer for timeout calculations */
  struct Timer *timer;
  /* Device address */
  uint64_t address;

  /* Scratchpad buffer */
  uint8_t scratchpad[9];
  /* Temperature resolution configuration */
  uint8_t resolution;
  /* Sensor state */
  uint8_t state;

  /* Reset sensor */
  bool reset;
  /* Start conversion */
  bool start;
  /* Stop after current operation */
  bool stop;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_DS18B20_H_ */