/*
 * sht2x_thermometer.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/sht2x.h>
#include <dpm/sensors/sht2x_defs.h>
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
static bool thermoUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct SensorClass * const SHT2XThermometer =
    &(const struct SensorClass){
    .size = sizeof(struct SHT2XThermometer),
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
    .update = thermoUpdate
};
/*----------------------------------------------------------------------------*/
static enum Result thermoInit(void *object, const void *configBase)
{
  const struct SHT2XThermometerConfig * const config = configBase;
  assert(config != NULL);
  assert(config->parent != NULL);

  struct SHT2XThermometer * const sensor = object;

  sensor->callbackArgument = NULL;
  sensor->onErrorCallback = NULL;
  sensor->onResultCallback = NULL;
  sensor->onUpdateCallback = NULL;
  sensor->parent = config->parent;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void thermoDeinit([[maybe_unused]] void *object)
{
}
/*----------------------------------------------------------------------------*/
static const char *thermoGetFormat([[maybe_unused]] const void *object)
{
  return "i24q8";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus thermoGetStatus([[maybe_unused]] const void *object)
{
  return SENSOR_IDLE;
}
/*----------------------------------------------------------------------------*/
static void thermoSetCallbackArgument(void *object, void *argument)
{
  struct SHT2XThermometer * const sensor = object;
  sensor->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
static void thermoSetErrorCallback(void *object,
    void (*callback)(void *, enum SensorResult))
{
  struct SHT2XThermometer * const sensor = object;
  sensor->onErrorCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void thermoSetResultCallback(void *object,
    void (*callback)(void *, const void *, size_t))
{
  struct SHT2XThermometer * const sensor = object;
  sensor->onResultCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void thermoSetUpdateCallback(void *object, void (*callback)(void *))
{
  struct SHT2XThermometer * const sensor = object;
  sensor->onUpdateCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void thermoReset([[maybe_unused]] void *object)
{
}
/*----------------------------------------------------------------------------*/
static void thermoSample(void *object)
{
  struct SHT2XThermometer * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->parent->flags, FLAG_THERMO_SAMPLE);
}
/*----------------------------------------------------------------------------*/
static void thermoStart(void *object)
{
  struct SHT2XThermometer * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->parent->flags, FLAG_THERMO_LOOP);
}
/*----------------------------------------------------------------------------*/
static void thermoStop(void *object)
{
  struct SHT2XThermometer * const sensor = object;
  atomicFetchAnd(&sensor->parent->flags,
      ~(FLAG_THERMO_LOOP | FLAG_THERMO_SAMPLE));
}
/*----------------------------------------------------------------------------*/
static bool thermoUpdate([[maybe_unused]] void *object)
{
  return false;
}
