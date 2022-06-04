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

    entry->sensor = 0;
  }
}
/*----------------------------------------------------------------------------*/
static void shOnResult(void *argument, const void *buffer, size_t length)
{
  struct SHEntry * const entry = argument;
  struct SensorHandler * const handler = entry->handler;

  if (handler->callback)
    handler->callback(handler->callbackArgument, entry->tag, buffer, length);
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

  if (handler->current != 0)
  {
    atomicFetchAnd(&handler->updating, ~handler->current->mask);
    handler->busy = sensorUpdate(handler->current->sensor);

    if (!handler->busy)
      handler->current = 0;
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
      handler->current = 0;
  }
}
/*----------------------------------------------------------------------------*/
bool shInit(struct SensorHandler *handler, size_t capacity, void *wq)
{
  handler->sensors = malloc(sizeof(struct SHEntry) * capacity);
  if (!handler->sensors)
    return false;

  for (size_t index = 0; index < capacity; ++index)
  {
    handler->sensors[index].handler = handler;
    handler->sensors[index].sensor = 0;
    handler->sensors[index].mask = 1UL << index;
  }

  handler->capacity = capacity;
  handler->pool = (1UL << capacity) - 1;
  handler->detaching = 0;
  handler->updating = 0;
  handler->busy = false;

  handler->current = 0;
  handler->wq = wq ? wq : WQ_DEFAULT;
  handler->callback = 0;

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
      sensorSetCallbackArgument(sensor, 0);
      sensorSetErrorCallback(sensor, 0);
      sensorSetResultCallback(sensor, 0);
      sensorSetUpdateCallback(sensor, 0);

      atomicFetchOr(&handler->detaching, entry->mask);
      wqAdd(handler->wq, shOnDetach, handler);
      break;
    }
  }
}
/*----------------------------------------------------------------------------*/
void shSetCallback(struct SensorHandler *handler,
    void (*callback)(void *, int, const void *, size_t), void *argument)
{
  handler->callbackArgument = argument;
  handler->callback = callback;
}
