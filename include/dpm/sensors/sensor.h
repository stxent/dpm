/*
 * sensors/sensor.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_SENSOR_H_
#define DPM_SENSORS_SENSOR_H_
/*----------------------------------------------------------------------------*/
#include <xcore/entity.h>
/*----------------------------------------------------------------------------*/
enum SensorResult
{
  SENSOR_OK,
  SENSOR_CALIBRATION_ERROR,
  SENSOR_DATA_ERROR,
  SENSOR_DATA_OVERFLOW,
  SENSOR_INTERFACE_ERROR,
  SENSOR_INTERFACE_TIMEOUT
};

enum SensorStatus
{
  SENSOR_IDLE,
  SENSOR_BUSY,
  SENSOR_ERROR
};

/* Class descriptor */
struct SensorClass
{
  CLASS_HEADER

  const char *(*getFormat)(const void *);
  enum SensorStatus (*getStatus)(const void *);
  void (*setCallbackArgument)(void *, void *);
  void (*setErrorCallback)(void *, void (*)(void *, enum SensorResult));
  void (*setResultCallback)(void *, void (*)(void *, const void *, size_t));
  void (*setUpdateCallback)(void *, void (*)(void *));

  void (*reset)(void *);
  void (*sample)(void *);
  void (*start)(void *);
  void (*stop)(void *);
  void (*suspend)(void *);
  bool (*update)(void *);
};

struct Sensor
{
  struct Entity base;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

/**
 * Get format of a measurement results.
 * @param sensor Pointer to a Sensor object.
 * @return Format of the output data.
 */
static inline const char *sensorGetFormat(const void *sensor)
{
  return ((const struct SensorClass *)CLASS(sensor))->getFormat(sensor);
}

/**
 * Get a status of the sensor.
 * @param sensor Pointer to a Sensor object.
 * @return Sensor status.
 */
static inline enum SensorStatus sensorGetStatus(const void *sensor)
{
  return ((const struct SensorClass *)CLASS(sensor))->getStatus(sensor);
}

/**
 * Set a callback argument for all sensor callbacks.
 * @param sensor Pointer to a Sensor object.
 * @param argument Callback function argument.
 */
static inline void sensorSetCallbackArgument(void *sensor, void *argument)
{
  ((const struct SensorClass *)CLASS(sensor))->setCallbackArgument(sensor,
      argument);
}

/**
 * Set a callback which is called in case of errors.
 * @param sensor Pointer to a Sensor object.
 * @param callback Callback function.
 */
static inline void sensorSetErrorCallback(void *sensor,
    void (*callback)(void *, enum SensorResult))
{
  ((const struct SensorClass *)CLASS(sensor))->setErrorCallback(sensor,
      callback);
}

/**
 * Set a callback which is called in the end of a successful measurement.
 * @param sensor Pointer to a Sensor object.
 * @param callback Callback function.
 */
static inline void sensorSetResultCallback(void *sensor,
    void (*callback)(void *, const void *, size_t))
{
  ((const struct SensorClass *)CLASS(sensor))->setResultCallback(sensor,
      callback);
}

/**
 * Set an update request callback.
 * @param sensor Pointer to a Sensor object.
 * @param callback Callback function.
 */
static inline void sensorSetUpdateCallback(void *sensor,
    void (*callback)(void *))
{
  ((const struct SensorClass *)CLASS(sensor))->setUpdateCallback(sensor,
      callback);
}

/**
 * Resets the sensor.
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorReset(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->reset(sensor);
}

/**
 * Make a single measurement.
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorSample(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->sample(sensor);
}

/**
 * Starts an automatic measurements.
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorStart(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->start(sensor);
}

/**
 * Stops an automatic measurements.
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorStop(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->stop(sensor);
}

/**
 * Puts the sensor in a power saving mode.
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorSuspend(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->suspend(sensor);
}

/**
 * Update a sensor state.
 * @param sensor Pointer to a Sensor object.
 * @return Bus status, @b true when the bus is busy and @b false when the bus
 * is idle and may be used by another sensor.
 */
static inline bool sensorUpdate(void *sensor)
{
  return ((const struct SensorClass *)CLASS(sensor))->update(sensor);
}

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_SENSOR_H_ */
