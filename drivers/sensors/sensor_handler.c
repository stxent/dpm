/*
 * sensor_handler.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/sensor_handler.h>
#include <halm/wq.h>
#include <xcore/accel.h>
#include <xcore/atomic.h>
#include <assert.h>
#include <stdlib.h>
/*----------------------------------------------------------------------------*/
static void invokeUpdate(struct SensorHandler *);
static void shOnError(void *, enum SensorResult);
static void shOnResult(void *, const void *, size_t);
static void shOnUpdate(void *);
static void shUpdate(void *);
static void updateTask(void *);
/*----------------------------------------------------------------------------*/
static void invokeUpdate(struct SensorHandler *handler)
{
  assert(handler->updateCallback != nullptr || handler->wq != nullptr);

  if (handler->updateCallback != nullptr)
  {
    handler->updateCallback(handler->updateCallbackArgument);
  }
  else if (!handler->pending)
  {
    handler->pending = true;

    if (wqAdd(handler->wq, updateTask, handler) != E_OK)
      handler->pending = false;
  }
}
/*----------------------------------------------------------------------------*/
static void shOnError(void *argument, enum SensorResult error)
{
  struct SHEntry * const entry = argument;
  struct SensorHandler * const handler = entry->handler;

  if (handler->errorCallback != nullptr)
  {
    handler->errorCallback(handler->errorCallbackArgument);
  }

  if (handler->failureCallback != nullptr)
  {
    handler->failureCallback(handler->failureCallbackArgument,
        entry->tag, error);
  }
}
/*----------------------------------------------------------------------------*/
static void shOnResult(void *argument, const void *buffer, size_t length)
{
  struct SHEntry * const entry = argument;
  struct SensorHandler * const handler = entry->handler;

  if (handler->dataCallback != nullptr)
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
  bool invoke = false;

  if (handler->busy)
  {
    if (handler->current == entry)
      invoke = true;
  }
  else
  {
    if (updating == 0)
      invoke = true;
  }

  if (invoke)
    invokeUpdate(handler);
}
/*----------------------------------------------------------------------------*/
static void shUpdate(void *argument)
{
  struct SensorHandler * const handler = argument;

  if (handler->current != nullptr)
  {
    atomicFetchAnd(&handler->updating, ~handler->current->mask);
    handler->busy = sensorUpdate(handler->current->sensor);

    if (!handler->busy)
      handler->current = nullptr;
  }

  if (!handler->busy)
  {
    while (handler->detaching)
    {
      const uint32_t index = 31 - countLeadingZeros32(handler->detaching);
      struct SHEntry * const entry = &handler->sensors[index];

      sensorSetErrorCallback(entry->sensor, nullptr);
      sensorSetResultCallback(entry->sensor, nullptr);
      sensorSetUpdateCallback(entry->sensor, nullptr);
      sensorSetCallbackArgument(entry->sensor, nullptr);
      entry->sensor = nullptr;

      atomicFetchAnd(&handler->detaching, ~entry->mask);
      atomicFetchAnd(&handler->updating, ~entry->mask);
      atomicFetchOr(&handler->pool, entry->mask);
    }
  }

  while (!handler->busy && handler->updating != 0)
  {
    const uint32_t index = 31 - countLeadingZeros32(handler->updating);
    struct SHEntry * const entry = &handler->sensors[index];

    atomicFetchAnd(&handler->updating, ~entry->mask);
    handler->busy = sensorUpdate(entry->sensor);

    if (handler->busy)
      handler->current = entry;
    else
      handler->current = nullptr;
  }

  if (!handler->busy && handler->idleCallback != nullptr)
    handler->idleCallback(handler->idleCallbackArgument);
}
/*----------------------------------------------------------------------------*/
static void updateTask(void *argument)
{
  struct SensorHandler * const handler = argument;

  handler->pending = false;
  shUpdate(handler);
}
/*----------------------------------------------------------------------------*/
bool shInit(struct SensorHandler *handler, size_t capacity)
{
  handler->sensors = malloc(sizeof(struct SHEntry) * capacity);
  if (handler->sensors == nullptr)
    return false;

  for (size_t index = 0; index < capacity; ++index)
  {
    handler->sensors[index].handler = handler;
    handler->sensors[index].sensor = nullptr;
    handler->sensors[index].mask = 1UL << index;
  }

  handler->capacity = capacity;
  handler->pool = (1UL << capacity) - 1;
  handler->detaching = 0;
  handler->updating = 0;
  handler->busy = false;
  handler->pending = false;

  handler->current = nullptr;
  handler->wq = nullptr;

  handler->dataCallback = nullptr;
  handler->dataCallbackArgument = nullptr;
  handler->failureCallback = nullptr;
  handler->failureCallbackArgument = nullptr;

  handler->errorCallback = nullptr;
  handler->errorCallbackArgument = nullptr;
  handler->idleCallback = nullptr;
  handler->idleCallbackArgument = nullptr;
  handler->updateCallback = nullptr;
  handler->updateCallbackArgument = nullptr;

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
  /*
   * Sensor should be stopped before detaching to avoid timer callbacks. Sensor
   * will be removed from the sensor list in the event loop, all bus transfers
   * will be completed at that point in time.
   */

  for (size_t index = 0; index < handler->capacity; ++index)
  {
    struct SHEntry * const entry = &handler->sensors[index];

    if (entry->sensor == sensor)
    {
      atomicFetchOr(&handler->detaching, entry->mask);
      invokeUpdate(handler);
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
void shSetFailureCallback(struct SensorHandler *handler,
    void (*callback)(void *, int, enum SensorResult), void *argument)
{
  handler->failureCallbackArgument = argument;
  handler->failureCallback = callback;
}
/*----------------------------------------------------------------------------*/
void shSetErrorCallback(void *object, void (*callback)(void *), void *argument)
{
  struct SensorHandler * const handler = object;

  assert(callback != nullptr);
  assert(handler->wq == nullptr);

  handler->errorCallbackArgument = argument;
  handler->errorCallback = callback;
}
/*----------------------------------------------------------------------------*/
void shSetIdleCallback(void *object, void (*callback)(void *), void *argument)
{
  struct SensorHandler * const handler = object;

  assert(callback != nullptr);
  assert(handler->wq == nullptr);

  handler->idleCallbackArgument = argument;
  handler->idleCallback = callback;
}
/*----------------------------------------------------------------------------*/
void shSetUpdateCallback(void *object, void (*callback)(void *), void *argument)
{
  struct SensorHandler * const handler = object;

  assert(callback != nullptr);
  assert(handler->wq == nullptr);

  handler->updateCallbackArgument = argument;
  handler->updateCallback = callback;
}
/*----------------------------------------------------------------------------*/
void shSetUpdateWorkQueue(void *object, struct WorkQueue *wq)
{
  struct SensorHandler * const handler = object;

  assert(wq != nullptr);
  assert(handler->updateCallback == nullptr);

  handler->wq = wq;
}
