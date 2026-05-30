/*
 * sensors/sensor.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_SENSOR_H_
#define DPM_SENSORS_SENSOR_H_
/*----------------------------------------------------------------------------*/
#include <xcore/entity.h>
#include <stdint.h>
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
  uint64_t (*getTimestamp)(const void *);
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
 * Get the data format of measurement results.
 *
 * @param sensor Pointer to a Sensor object.
 * @return A null‑terminated string representing the data format.
 */
static inline const char *sensorGetFormat(const void *sensor)
{
  return ((const struct SensorClass *)CLASS(sensor))->getFormat(sensor);
}

/**
 * Get the current status of the sensor.
 *
 * @param sensor Pointer to a Sensor object.
 * @return The current sensor status as a value of the enum SensorStatus.
 */
static inline enum SensorStatus sensorGetStatus(const void *sensor)
{
  return ((const struct SensorClass *)CLASS(sensor))->getStatus(sensor);
}

/**
 * Get the timestamp for the last measurement of the sensor.
 *
 * This function retrieves the timestamp associated with the most recent
 * measurement taken by the sensor. The timestamp is typically represented
 * as a 64‑bit value indicating the time in microseconds since system boot.
 *
 * @param sensor Pointer to a Sensor object. Must not be NULL.
 * @return The timestamp for the last measurement, if timestamps are supported
 * by the sensor. If the sensor does not support timestamps or an error occurs,
 * the function returns 0.
 */
static inline uint64_t sensorGetTimestamp(const void *sensor)
{
  return ((const struct SensorClass *)CLASS(sensor))->getTimestamp(sensor);
}

/**
 * Set a user-defined argument that will be passed to all callback functions
 * associated with this sensor.
 *
 * @param sensor Pointer to a Sensor object.
 * @param argument User-defined argument (context data) to be passed
 * to callbacks. Can be NULL.
 */
static inline void sensorSetCallbackArgument(void *sensor, void *argument)
{
  ((const struct SensorClass *)CLASS(sensor))->setCallbackArgument(sensor,
      argument);
}

/**
 * Set the callback function that is invoked when a sensor error occurs.
 *
 * The callback is called asynchronously when an internal error is detected
 * during operation.
 *
 * @param sensor Pointer to a Sensor object.
 * @param callback Function pointer to the error callback.
 * Pass NULL to disable the error callback.
 */
static inline void sensorSetErrorCallback(void *sensor,
    void (*callback)(void *, enum SensorResult))
{
  ((const struct SensorClass *)CLASS(sensor))->setErrorCallback(sensor,
      callback);
}

/**
 * Set the callback function that is invoked upon successful completion
 * of a measurement.
 *
 * @param sensor Pointer to a Sensor object.
 * @param callback Function pointer to the result callback. The 'data' pointer
 * is valid only within the callback. Pass NULL to disable the result callback.
 */
static inline void sensorSetResultCallback(void *sensor,
    void (*callback)(void *, const void *, size_t))
{
  ((const struct SensorClass *)CLASS(sensor))->setResultCallback(sensor,
      callback);
}

/**
 * Set the callback function that requests an update cycle from the system.
 *
 * @param sensor Pointer to a Sensor object.
 * @param callback Function pointer to the update request callback.
 */
static inline void sensorSetUpdateCallback(void *sensor,
    void (*callback)(void *))
{
  ((const struct SensorClass *)CLASS(sensor))->setUpdateCallback(sensor,
      callback);
}

/**
 * Reset the sensor to its initial state.
 *
 * This function performs a hardware or software reset, clearing any internal
 * state and errors.
 *
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorReset(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->reset(sensor);
}

/**
 * Trigger a single measurement cycle.
 *
 * This function initiates one shot measurement. The result will be delivered
 * via the result callback.
 *
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorSample(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->sample(sensor);
}

/**
 * Start continuous automatic measurements.
 *
 * The sensor will perform measurements at a predefined rate until stopped by
 * sensorStop(). Results are delivered asynchronously via the result callback.
 *
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorStart(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->start(sensor);
}

/**
 * Stop continuous automatic measurements.
 *
 * Halts any ongoing measurement cycle initiated by sensorStart().
 * Does nothing if the sensor is not in a running state.
 *
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorStop(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->stop(sensor);
}

/**
 * Put the sensor into a low‑power (suspended) mode.
 *
 * All active operations are halted, and the sensor draws minimal power.
 *
 * @param sensor Pointer to a Sensor object.
 */
static inline void sensorSuspend(void *sensor)
{
  ((const struct SensorClass *)CLASS(sensor))->suspend(sensor);
}

/**
 * Update the internal state of the sensor and check bus availability.
 *
 * This non‑blocking function processes any pending I/O and returns
 * the current bus status.
 *
 * @param sensor Pointer to a Sensor object.
 * @return Bus status: @b true if the bus is busy with this sensor's operation,
 * @b false if the bus is idle and available for use by other devices.
 */
static inline bool sensorUpdate(void *sensor)
{
  return ((const struct SensorClass *)CLASS(sensor))->update(sensor);
}

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_SENSOR_H_ */
