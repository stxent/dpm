/*
 * sensors/mpu60xx.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_MPU60XX_H_
#define DPM_SENSORS_MPU60XX_H_
/*----------------------------------------------------------------------------*/
#include <dpm/sensors/sensor.h>
#include <halm/pin.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const MPU60XX;
extern const struct SensorClass * const MPU60XXAccelerometer;
extern const struct SensorClass * const MPU60XXGyroscope;
extern const struct SensorClass * const MPU60XXThermometer;

struct Interface;
struct Interrupt;
struct Timer;

enum [[gnu::packed]] MPU60XXAccelScale
{
  MPU60XX_ACCEL_DEFAULT,
  MPU60XX_ACCEL_2,
  MPU60XX_ACCEL_4,
  MPU60XX_ACCEL_8,
  MPU60XX_ACCEL_16,

  MPU60XX_ACCEL_END
};

enum [[gnu::packed]] MPU60XXGyroScale
{
  MPU60XX_GYRO_DEFAULT,
  MPU60XX_GYRO_250,
  MPU60XX_GYRO_500,
  MPU60XX_GYRO_1000,
  MPU60XX_GYRO_2000,

  MPU60XX_GYRO_END
};

struct MPU60XXConfig
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
  /** Optional: pin used as Chip Select output. */
  PinNumber cs;

  /** Mandatory: sample rate for both accelerometer and gyroscope. */
  uint16_t sampleRate;
  /** Optional: accelerometer scale configuration. */
  enum MPU60XXAccelScale accelScale;
  /** Optional: gyroscope scale configuration. */
  enum MPU60XXGyroScale gyroScale;
};

struct MPU60XXProxy;

struct MPU60XX
{
  struct Entity base;

  /* Active proxy */
  struct MPU60XXProxy *active;
  /* Accelerometer sensor proxy */
  struct MPU60XXProxy *accelerometer;
  /* Gyroscope sensor proxy */
  struct MPU60XXProxy *gyroscope;
  /* Thermometer sensor proxy */
  struct MPU60XXProxy *thermometer;

  /* Sensor bus */
  struct Interface *bus;
  /* External event */
  struct Interrupt *event;
  /* Timer for periodic events */
  struct Timer *timer;
  /* Chip Select output */
  struct Pin gpio;
  /* Bus address */
  uint32_t address;
  /* Baud rate of the serial interface */
  uint32_t rate;

  /* Sample rate settings */
  uint16_t sampleRate;
  /* Accelerometer scale settings */
  uint8_t accelScale;
  /* Gyroscope scale settings */
  uint8_t gyroScale;

  /* Buffer for received data */
  uint8_t buffer[14];
  /* Command and status flags */
  uint16_t flags;
  /* Current operation */
  uint8_t state;
  /* Configuration step */
  uint8_t step;
};

struct MPU60XXProxyConfig
{
  /** Mandatory: parent object. */
  struct MPU60XX *parent;
};

struct MPU60XXProxy
{
  struct Sensor base;

  void *callbackArgument;
  void (*onErrorCallback)(void *, enum SensorResult);
  void (*onResultCallback)(void *, const void *, size_t);
  void (*onUpdateCallback)(void *);

  /* Parent object */
  struct MPU60XX *parent;
  /* Proxy type */
  uint8_t type;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

struct MPU60XXProxy *mpu60xxMakeAccelerometer(struct MPU60XX *);
struct MPU60XXProxy *mpu60xxMakeGyroscope(struct MPU60XX *);
struct MPU60XXProxy *mpu60xxMakeThermometer(struct MPU60XX *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_MPU60XX_H_ */
