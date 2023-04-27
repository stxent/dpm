/*
 * sensor_handler.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/sensor_handler.h>
#include <halm/wq.h>
#include <xcore/accel.h>
#include <xcore/atomic.h>
#include <stdlib.h>
/*----------------------------------------------------------------------------*/
static void shOnUpdate(void *);
static void shOnDetach(void *);
static void shOnError(void *, enum SensorResult);
static void shOnResult(void *, const void *, size_t);
static void shUpdate(void *);
/*----------------------------------------------------------------------------*/
static void shOnDetach(void *argument)
{
  struct SensorHandler * const handler = argument;

  while (!handler->busy && handler->detaching)
  {
    const uint32_t index = 31 - countLeadingZeros32(handler->updating);
    struct SHEntry * const entry = &handler->sensors[index];

    atomicFetchAnd(&handler->detaching, ~entry->mask);
    atomicFetchAnd(&handler->updating, ~entry->mask);
    atomicFetchOr(&handler->pool, entry->mask);

    entry->sensor = NULL;
  }
}
/*----------------------------------------------------------------------------*/
static void shOnError(void *argument, enum SensorResult error)
{
  struct SHEntry * const entry = argument;
  struct SensorHandler * const handler = entry->handler;

  if (handler->errorCallback != NULL)
  {
    handler->errorCallback(handler->errorCallbackArgument,
        entry->tag, error);
  }
}
/*----------------------------------------------------------------------------*/
static void shOnResult(void *argument, const void *buffer, size_t length)
{
  struct SHEntry * const entry = argument;
  struct SensorHandler * const handler = entry->handler;

  if (handler->dataCallback != NULL)
  {
    handler->dataCallback(handler->dataCallbackArgument,
        entry->tag, buffer, length);
  }
}
/*----------------------------------------------------------------------------*/
static void shOnUpdate(void *argument)
{
  struct SHEntry * const entry = argument;
  struct SensorHandler * const handler = entry->handler;

  const uint32_t updating = atomicFetchOr(&handler->updating, entry->mask);

  if (handler->busy)
  {
    if (handler->current == entry)
      wqAdd(handler->wq, shUpdate, handler);
  }
  else
  {
    if (updating == 0)
      wqAdd(handler->wq, shUpdate, handler);
  }
}
/*----------------------------------------------------------------------------*/
static void shUpdate(void *argument)
{
  struct SensorHandler * const handler = argument;

  if (handler->current != NULL)
  {
    atomicFetchAnd(&handler->updating, ~handler->current->mask);
    handler->busy = sensorUpdate(handler->current->sensor);

    if (!handler->busy)
      handler->current = NULL;
  }

  while (!handler->busy && handler->updating)
  {
    const uint32_t index = 31 - countLeadingZeros32(handler->updating);
    struct SHEntry * const entry = &handler->sensors[index];

    atomicFetchAnd(&handler->updating, ~entry->mask);
    handler->busy = sensorUpdate(entry->sensor);

    if (handler->busy)
      handler->current = entry;
    else
      handler->current = NULL;
  }
}
/*----------------------------------------------------------------------------*/
bool shInit(struct SensorHandler *handler, size_t capacity, void *wq)
{
  handler->sensors = malloc(sizeof(struct SHEntry) * capacity);
  if (handler->sensors == NULL)
    return false;

  for (size_t index = 0; index < capacity; ++index)
  {
    handler->sensors[index].handler = handler;
    handler->sensors[index].sensor = NULL;
    handler->sensors[index].mask = 1UL << index;
  }

  handler->capacity = capacity;
  handler->pool = (1UL << capacity) - 1;
  handler->detaching = 0;
  handler->updating = 0;
  handler->busy = false;

  handler->current = NULL;
  handler->wq = wq ? wq : WQ_DEFAULT;
  handler->dataCallback = NULL;
  handler->errorCallback = NULL;

  return true;
}
/*----------------------------------------------------------------------------*/
void shDeinit(struct SensorHandler *handler)
{
  free(handler->sensors);
}
/*----------------------------------------------------------------------------*/
bool shAttach(struct SensorHandler *handler, void *sensor, int tag)
{
  while (handler->pool)
  {
    const unsigned int channel = 31 - countLeadingZeros32(handler->pool);
    const uint32_t mask = 1UL << channel;
    const uint32_t pool = atomicFetchAnd(&handler->pool, ~mask);

    if (!(pool & mask))
      continue;

    struct SHEntry * const entry = &handler->sensors[channel];
    entry->sensor = sensor;
    entry->tag = tag;

    sensorSetCallbackArgument(sensor, entry);
    sensorSetErrorCallback(sensor, shOnError);
    sensorSetResultCallback(sensor, shOnResult);
    sensorSetUpdateCallback(sensor, shOnUpdate);

    return true;
  }

  return false;
}
/*----------------------------------------------------------------------------*/
void shDetach(struct SensorHandler *handler, void *sensor)
{
  for (size_t index = 0; index < handler->capacity; ++index)
  {
    struct SHEntry * const entry = &handler->sensors[index];

    if (entry->sensor == sensor)
    {
      sensorSetCallbackArgument(sensor, NULL);
      sensorSetErrorCallback(sensor, NULL);
      sensorSetResultCallback(sensor, NULL);
      sensorSetUpdateCallback(sensor, NULL);

      atomicFetchOr(&handler->detaching, entry->mask);
      wqAdd(handler->wq, shOnDetach, handler);
      break;
    }
  }
}
/*----------------------------------------------------------------------------*/
void shSetDataCallback(struct SensorHandler *handler,
    void (*callback)(void *, int, const void *, size_t), void *argument)
{
  handler->dataCallbackArgument = argument;
  handler->dataCallback = callback;
}
/*----------------------------------------------------------------------------*/
void shSetErrorCallback(struct SensorHandler *handler,
    void (*callback)(void *, int, enum SensorResult), void *argument)
{
  handler->errorCallbackArgument = argument;
  handler->errorCallback = callback;
}
