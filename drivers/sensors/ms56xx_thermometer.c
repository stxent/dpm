/*
 * ms56xx_thermometer.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/ms56xx.h>
#include <dpm/sensors/ms56xx_defs.h>
#include <xcore/atomic.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static enum Result thermoInit(void *, const void *);
static void thermoDeinit(void *);
static const char *thermoGetFormat(const void *);
static enum SensorStatus thermoGetStatus(const void *);
static void thermoSetCallbackArgument(void *, void *);
static void thermoSetErrorCallback(void *, void (*)(void *, enum SensorResult));
static void thermoSetResultCallback(void *,
    void (*)(void *, const void *, size_t));
static void thermoSetUpdateCallback(void *, void (*)(void *));
static void thermoReset(void *);
static void thermoSample(void *);
static void thermoStart(void *);
static void thermoStop(void *);
static void thermoSuspend(void *);
static bool thermoUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct SensorClass * const MS56XXThermometer =
    &(const struct SensorClass){
    .size = sizeof(struct MS56XXThermometer),
    .init = thermoInit,
    .deinit = thermoDeinit,

    .getFormat = thermoGetFormat,
    .getStatus = thermoGetStatus,
    .setCallbackArgument = thermoSetCallbackArgument,
    .setErrorCallback = thermoSetErrorCallback,
    .setResultCallback = thermoSetResultCallback,
    .setUpdateCallback = thermoSetUpdateCallback,
    .reset = thermoReset,
    .sample = thermoSample,
    .start = thermoStart,
    .stop = thermoStop,
    .suspend = thermoSuspend,
    .update = thermoUpdate
};
/*----------------------------------------------------------------------------*/
static enum Result thermoInit(void *object, const void *configBase)
{
  const struct MS56XXThermometerConfig * const config = configBase;
  assert(config != NULL);
  assert(config->parent != NULL);

  struct MS56XXThermometer * const sensor = object;

  sensor->callbackArgument = NULL;
  sensor->onErrorCallback = NULL;
  sensor->onResultCallback = NULL;
  sensor->onUpdateCallback = NULL;
  sensor->parent = config->parent;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void thermoDeinit(void *object __attribute__((unused)))
{
}
/*----------------------------------------------------------------------------*/
static const char *thermoGetFormat(
    const void *object __attribute__((unused)))
{
  return "i24q8";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus thermoGetStatus(
    const void *object __attribute__((unused)))
{
  return SENSOR_IDLE;
}
/*----------------------------------------------------------------------------*/
static void thermoSetCallbackArgument(void *object, void *argument)
{
  struct MS56XXThermometer * const sensor = object;
  sensor->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
static void thermoSetErrorCallback(void *object,
    void (*callback)(void *, enum SensorResult))
{
  struct MS56XXThermometer * const sensor = object;
  sensor->onErrorCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void thermoSetResultCallback(void *object,
    void (*callback)(void *, const void *, size_t))
{
  struct MS56XXThermometer * const sensor = object;
  sensor->onResultCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void thermoSetUpdateCallback(void *object, void (*callback)(void *))
{
  struct MS56XXThermometer * const sensor = object;
  sensor->onUpdateCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void thermoReset(void *object __attribute__((unused)))
{
}
/*----------------------------------------------------------------------------*/
static void thermoSample(void *object)
{
  struct MS56XXThermometer * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->parent->flags, FLAG_THERMO_SAMPLE);
}
/*----------------------------------------------------------------------------*/
static void thermoStart(void *object)
{
  struct MS56XXThermometer * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->parent->flags, FLAG_THERMO_LOOP);
}
/*----------------------------------------------------------------------------*/
static void thermoStop(void *object)
{
  struct MS56XXThermometer * const sensor = object;
  atomicFetchAnd(&sensor->parent->flags,
      ~(FLAG_THERMO_LOOP | FLAG_THERMO_SAMPLE));
}
/*----------------------------------------------------------------------------*/
static void thermoSuspend(void *object __attribute__((unused)))
{
}
/*----------------------------------------------------------------------------*/
static bool thermoUpdate(void *object __attribute__((unused)))
{
  return false;
}
