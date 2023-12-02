/*
 * sensors/sensor_handler.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_SENSOR_HANDLER_H_
#define DPM_SENSORS_SENSOR_HANDLER_H_
/*----------------------------------------------------------------------------*/
#include <dpm/sensors/sensor.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
struct WorkQueue;

struct SHEntry
{
  void *handler;
  void *sensor;
  uint32_t mask;
  int tag;
};

struct SensorHandler
{
  struct SHEntry *current;
  struct SHEntry *sensors;

  /* Sensor interface */
  void (*dataCallback)(void *, int, const void *, size_t);
  void *dataCallbackArgument;
  void (*failureCallback)(void *, int, enum SensorResult);
  void *failureCallbackArgument;

  /* Optional Bus Handler executor interface */
  void (*errorCallback)(void *);
  void *errorCallbackArgument;
  void (*idleCallback)(void *);
  void *idleCallbackArgument;
  void (*updateCallback)(void *);
  void *updateCallbackArgument;

  /* Optional Work Queue executor interface */
  void *wq;

  size_t capacity;
  uint32_t pool;
  uint32_t detaching;
  uint32_t updating;
  bool busy;
  bool pending;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

bool shInit(struct SensorHandler *, size_t);
void shDeinit(struct SensorHandler *);
bool shAttach(struct SensorHandler *, void *, int);
void shDetach(struct SensorHandler *, void *);

/* Sensor data interface */
void shSetDataCallback(struct SensorHandler *,
    void (*)(void *, int, const void *, size_t), void *);
void shSetFailureCallback(struct SensorHandler *,
    void (*)(void *, int, enum SensorResult), void *);

/* Executor interface */
void shSetErrorCallback(void *, void (*)(void *), void *);
void shSetIdleCallback(void *, void (*)(void *), void *);
void shSetUpdateCallback(void *, void (*)(void *), void *);
void shSetUpdateWorkQueue(void *, struct WorkQueue *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_SENSOR_HANDLER_H_ */
