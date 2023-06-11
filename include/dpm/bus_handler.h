/*
 * bus_handler.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_BUS_HANDLER_H_
#define DPM_BUS_HANDLER_H_
/*----------------------------------------------------------------------------*/
#include <xcore/helpers.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
typedef void (*BHCallback)(void *, void *);
typedef bool (*BHDeviceCallback)(void *);
typedef void (*BHDeviceCallbackSetter)(void *, void (*)(void *), void *);

struct BHEntry
{
  void *handler;
  void *device;
  uint32_t mask;
  BHDeviceCallbackSetter errorCallbackSetter;
  BHDeviceCallbackSetter idleCallbackSetter;
  BHDeviceCallbackSetter updateCallbackSetter;
  BHDeviceCallback updateCallback;
};

struct BusHandler
{
  struct BHEntry *current;
  struct BHEntry *devices;
  void *wq;

  BHCallback errorCallback;
  void *errorCallbackArgument;
  BHCallback idleCallback;
  void *idleCallbackArgument;

  size_t capacity;
  uint32_t pool;
  uint32_t detaching;
  uint32_t updating;
  bool busy;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

bool bhInit(struct BusHandler *, size_t, void *);
void bhDeinit(struct BusHandler *);
bool bhAttach(struct BusHandler *, void *, BHDeviceCallbackSetter,
    BHDeviceCallbackSetter, BHDeviceCallbackSetter, BHDeviceCallback);
void bhDetach(struct BusHandler *, void *);
void bhSetErrorCallback(struct BusHandler *, BHCallback, void *);
void bhSetIdleCallback(struct BusHandler *, BHCallback, void *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_BUS_HANDLER_H_ */
