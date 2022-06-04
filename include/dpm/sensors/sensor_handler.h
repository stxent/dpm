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
  void *wq;

  void (*callback)(void *, int, const void *, size_t);
  void *callbackArgument;

  size_t capacity;
  uint32_t pool;
  uint32_t detaching;
  uint32_t updating;
  bool busy;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

bool shInit(struct SensorHandler *, size_t, void *);
void shDeinit(struct SensorHandler *);
bool shAttach(struct SensorHandler *, void *, int);
void shDetach(struct SensorHandler *, void *);
void shSetCallback(struct SensorHandler *,
    void (*)(void *, int, const void *, size_t), void *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_SENSOR_HANDLER_H_ */
