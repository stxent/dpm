/*
 * sensors/ms56xx.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_MS56XX_H_
#define DPM_SENSORS_MS56XX_H_
/*----------------------------------------------------------------------------*/
#include <dpm/sensors/sensor.h>
#include <halm/pin.h>
/*----------------------------------------------------------------------------*/
extern const struct SensorClass * const MS56XX;
extern const struct SensorClass * const MS56XXThermometer;

struct Interface;
struct Timer;

enum [[gnu::packed]] MS56XXOversampling
{
  MS56XX_OVERSAMPLING_DEFAULT,
  MS56XX_OVERSAMPLING_256,
  MS56XX_OVERSAMPLING_512,
  MS56XX_OVERSAMPLING_1024,
  MS56XX_OVERSAMPLING_2048,
  MS56XX_OVERSAMPLING_4096,

  MS56XX_OVERSAMPLING_END
};

enum [[gnu::packed]] MS56XXSubtype
{
  MS56XX_TYPE_5607,
  MS56XX_TYPE_5611
};

struct MS56XXConfig
{
  /** Mandatory: serial interface. */
  void *bus;
  /** Mandatory: event timer. */
  void *timer;

  /** Optional: sensor address. */
  uint32_t address;
  /** Optional: bit rate of the serial interface. */
  uint32_t rate;
  /** Optional: pin used as Chip Select output. */
  PinNumber cs;

  /** Optional: oversampling configuration. */
  enum MS56XXOversampling oversampling;
  /** Mandatory: sensor subtype. */
  enum MS56XXSubtype subtype;
};

struct MS56XXThermometer;

struct MS56XX
{
  struct Sensor base;

  void *callbackArgument;
  void (*onErrorCallback)(void *, enum SensorResult);
  void (*onResultCallback)(void *, const void *, size_t);
  void (*onUpdateCallback)(void *);

  void (*calculate)(const uint16_t *, int32_t, int64_t *, int64_t *);
  void (*compensate)(int32_t, int32_t, int64_t *, int32_t *, int64_t *);

  /* Thermometer sensor proxy */
  struct MS56XXThermometer *thermometer;

  /* Sensor bus */
  struct Interface *bus;
  /* Timer for periodic events */
  struct Timer *timer;
  /* Chip Select output */
  struct Pin gpio;
  /* Bus address */
  uint32_t address;
  /* Baud rate of the serial interface */
  uint32_t rate;

  /* Raw pressure value */
  uint32_t pressure;
  /* Raw temperature value */
  uint32_t temperature;

  /* PROM data */
  uint16_t prom[8];
  /* Buffer for received data */
  uint8_t buffer[3];
  /* Command and status flags */
  uint8_t flags;
  /* Oversampling settings */
  uint8_t oversampling;
  /* Current PROM position */
  uint8_t parameter;
  /* Current operation */
  uint8_t state;
};

struct MS56XXThermometerConfig
{
  /** Mandatory: parent object. */
  struct MS56XX *parent;
};

struct MS56XXThermometer
{
  struct Sensor base;

  void *callbackArgument;
  void (*onErrorCallback)(void *, enum SensorResult);
  void (*onResultCallback)(void *, const void *, size_t);
  void (*onUpdateCallback)(void *);

  struct MS56XX *parent;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

struct MS56XXThermometer *ms56xxMakeThermometer(struct MS56XX *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_MS56XX_H_ */
