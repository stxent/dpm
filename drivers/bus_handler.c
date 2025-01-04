/*
 * bus_handler.c
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/bus_handler.h>
#include <halm/wq.h>
#include <xcore/accel.h>
#include <xcore/atomic.h>
#include <assert.h>
#include <stdlib.h>
/*----------------------------------------------------------------------------*/
static void bhOnDetach(void *);
static void bhOnError(void *);
static void bhOnIdle(void *);
static void bhOnUpdate(void *);
static void bhUpdate(void *);
/*----------------------------------------------------------------------------*/
static void bhOnDetach(void *argument)
{
  struct BusHandler * const handler = argument;

  while (!handler->busy && handler->detaching)
  {
    const uint32_t index = 31 - countLeadingZeros32(handler->detaching);
    struct BHEntry * const entry = &handler->devices[index];

    atomicFetchAnd(&handler->detaching, ~entry->mask);
    atomicFetchAnd(&handler->updating, ~entry->mask);
    atomicFetchOr(&handler->pool, entry->mask);

    entry->device = NULL;
  }
}
/*----------------------------------------------------------------------------*/
static void bhOnError(void *argument)
{
  struct BHEntry * const entry = argument;
  struct BusHandler * const handler = entry->handler;

  if (handler->errorCallback != NULL)
    handler->errorCallback(handler->errorCallbackArgument, entry->device);
}
/*----------------------------------------------------------------------------*/
static void bhOnIdle(void *argument)
{
  struct BHEntry * const entry = argument;
  struct BusHandler * const handler = entry->handler;

  if (handler->idleCallback != NULL)
    handler->idleCallback(handler->idleCallbackArgument, entry->device);
}
/*----------------------------------------------------------------------------*/
static void bhOnUpdate(void *argument)
{
  struct BHEntry * const entry = argument;
  struct BusHandler * const handler = entry->handler;

  const uint32_t updating = atomicFetchOr(&handler->updating, entry->mask);

  if (handler->busy)
  {
    if (handler->current == entry)
      wqAdd(handler->wq, bhUpdate, handler);
  }
  else
  {
    if (updating == 0)
      wqAdd(handler->wq, bhUpdate, handler);
  }
}
/*----------------------------------------------------------------------------*/
static void bhUpdate(void *argument)
{
  struct BusHandler * const handler = argument;

  if (handler->current != NULL)
  {
    atomicFetchAnd(&handler->updating, ~handler->current->mask);
    handler->busy = handler->current->updateCallback(handler->current->device);

    if (!handler->busy)
      handler->current = NULL;
  }

  while (!handler->busy && handler->updating)
  {
    const uint32_t index = 31 - countLeadingZeros32(handler->updating);
    struct BHEntry * const entry = &handler->devices[index];

    atomicFetchAnd(&handler->updating, ~entry->mask);
    handler->busy = entry->updateCallback(entry->device);

    if (handler->busy)
      handler->current = entry;
    else
      handler->current = NULL;
  }
}
/*----------------------------------------------------------------------------*/
bool bhInit(struct BusHandler *handler, size_t capacity, void *wq)
{
  handler->devices = malloc(sizeof(struct BHEntry) * capacity);
  if (handler->devices == NULL)
    return false;

  for (size_t index = 0; index < capacity; ++index)
  {
    handler->devices[index].handler = handler;
    handler->devices[index].device = NULL;
    handler->devices[index].mask = 1UL << index;
  }

  handler->capacity = capacity;
  handler->pool = (1UL << capacity) - 1;
  handler->detaching = 0;
  handler->updating = 0;
  handler->busy = false;

  handler->current = NULL;
  handler->wq = wq ? wq : WQ_DEFAULT;
  handler->errorCallback = NULL;
  handler->idleCallback = NULL;

  return true;
}
/*----------------------------------------------------------------------------*/
void bhDeinit(struct BusHandler *handler)
{
  free(handler->devices);
}
/*----------------------------------------------------------------------------*/
bool bhAttach(struct BusHandler *handler, void *device,
    BHDeviceCallbackSetter errorCallbackSetter,
    BHDeviceCallbackSetter idleCallbackSetter,
    BHDeviceCallbackSetter updateCallbackSetter,
    BHDeviceCallback updateCallback)
{
  assert(device != NULL);
  assert(updateCallbackSetter != NULL);
  assert(updateCallback != NULL);

  while (handler->pool)
  {
    const unsigned int channel = 31 - countLeadingZeros32(handler->pool);
    const uint32_t mask = 1UL << channel;
    const uint32_t pool = atomicFetchAnd(&handler->pool, ~mask);

    if (!(pool & mask))
      continue;

    struct BHEntry * const entry = &handler->devices[channel];

    entry->device = device;
    entry->errorCallbackSetter = errorCallbackSetter;
    entry->idleCallbackSetter = idleCallbackSetter;
    entry->updateCallbackSetter = updateCallbackSetter;
    entry->updateCallback = updateCallback;

    if (entry->errorCallbackSetter != NULL)
      entry->errorCallbackSetter(device, bhOnError, entry);
    if (entry->idleCallbackSetter != NULL)
      entry->idleCallbackSetter(device, bhOnIdle, entry);
    entry->updateCallbackSetter(device, bhOnUpdate, entry);

    return true;
  }

  return false;
}
/*----------------------------------------------------------------------------*/
void bhDetach(struct BusHandler *handler, void *device)
{
  for (size_t index = 0; index < handler->capacity; ++index)
  {
    struct BHEntry * const entry = &handler->devices[index];

    if (entry->device == device)
    {
      if (entry->errorCallbackSetter != NULL)
        entry->errorCallbackSetter(device, NULL, NULL);
      if (entry->idleCallbackSetter != NULL)
        entry->idleCallbackSetter(device, NULL, NULL);
      entry->updateCallbackSetter(device, NULL, NULL);

      atomicFetchOr(&handler->detaching, entry->mask);
      wqAdd(handler->wq, bhOnDetach, handler);
      break;
    }
  }
}
/*----------------------------------------------------------------------------*/
void bhSetErrorCallback(struct BusHandler *handler, BHCallback callback,
    void *argument)
{
  handler->errorCallbackArgument = argument;
  handler->errorCallback = callback;
}
/*----------------------------------------------------------------------------*/
void bhSetIdleCallback(struct BusHandler *handler, BHCallback callback,
    void *argument)
{
  handler->idleCallbackArgument = argument;
  handler->idleCallback = callback;
}
