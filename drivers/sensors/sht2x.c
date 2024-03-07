/*
 * sht2x.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/sht2x.h>
#include <dpm/sensors/sht2x_defs.h>
#include <halm/timer.h>
#include <xcore/atomic.h>
#include <xcore/bits.h>
#include <xcore/interface.h>
#include <assert.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
#define LENGTH_COMMAND  1
#define LENGTH_CONFIG   2

enum State
{
  STATE_IDLE,

  STATE_CONFIG_WRITE,
  STATE_CONFIG_WRITE_WAIT,

  STATE_H_START,
  STATE_H_START_WAIT,
  STATE_H_WAIT,
  STATE_H_READ,
  STATE_H_READ_WAIT,

  STATE_T_START,
  STATE_T_START_WAIT,
  STATE_T_WAIT,
  STATE_T_READ,
  STATE_T_READ_WAIT,

  STATE_PROCESS,

  STATE_ERROR_WAIT,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
};
/*----------------------------------------------------------------------------*/
static void busInit(struct SHT2X *);
static void calcHumidity(struct SHT2X *);
static inline uint32_t calcResetTimeout(const struct Timer *);
static uint16_t fetchSample(const struct SHT2X *);
static void onBusEvent(void *);
static void onTimerEvent(void *);
static uint8_t resolutionToConfig(const struct SHT2X *);
static uint32_t resolutionToHumidityTime(const struct SHT2X *);
static uint32_t resolutionToTemperatureTime(const struct SHT2X *);
static void startConfigWrite(struct SHT2X *);
static void startHumidityConversion(struct SHT2X *);
static void startSampleRead(struct SHT2X *);
static void startTemperatureConversion(struct SHT2X *);

static enum Result shtInit(void *, const void *);
static void shtDeinit(void *);
static const char *shtGetFormat(const void *);
static enum SensorStatus shtGetStatus(const void *);
static void shtSetCallbackArgument(void *, void *);
static void shtSetErrorCallback(void *, void (*)(void *, enum SensorResult));
static void shtSetResultCallback(void *,
    void (*)(void *, const void *, size_t));
static void shtSetUpdateCallback(void *, void (*)(void *));
static void shtReset(void *);
static void shtSample(void *);
static void shtStart(void *);
static void shtStop(void *);
static void shtSuspend(void *);
static bool shtUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct SensorClass * const SHT2X = &(const struct SensorClass){
    .size = sizeof(struct SHT2X),
    .init = shtInit,
    .deinit = shtDeinit,

    .getFormat = shtGetFormat,
    .getStatus = shtGetStatus,
    .setCallbackArgument = shtSetCallbackArgument,
    .setErrorCallback = shtSetErrorCallback,
    .setResultCallback = shtSetResultCallback,
    .setUpdateCallback = shtSetUpdateCallback,
    .reset = shtReset,
    .sample = shtSample,
    .start = shtStart,
    .stop = shtStop,
    .suspend = shtSuspend,
    .update = shtUpdate
};
/*----------------------------------------------------------------------------*/
static void busInit(struct SHT2X *sensor)
{
  /* Lock the interface */
  ifSetParam(sensor->bus, IF_ACQUIRE, NULL);

  ifSetParam(sensor->bus, IF_ADDRESS, &sensor->address);
  ifSetParam(sensor->bus, IF_ZEROCOPY, NULL);
  ifSetCallback(sensor->bus, onBusEvent, sensor);

  if (sensor->rate)
    ifSetParam(sensor->bus, IF_RATE, &sensor->rate);

  /* Start bus watchdog */
  timerSetOverflow(sensor->timer, calcResetTimeout(sensor->timer));
  timerSetValue(sensor->timer, 0);
  timerEnable(sensor->timer);
}
/*----------------------------------------------------------------------------*/
static void calcHumidity(struct SHT2X *sensor)
{
  const int32_t temperature =
      (((int32_t)sensor->temperature * 11246) >> 14) - 11993;
  const int16_t humidity =
      (((int32_t)sensor->humidity * 16000) >> 15) - 1536;

  sensor->onResultCallback(sensor->callbackArgument,
      &humidity, sizeof(humidity));

  if (atomicLoad(&sensor->flags) & (FLAG_THERMO_LOOP | FLAG_THERMO_SAMPLE))
  {
    sensor->thermometer->onResultCallback(sensor->thermometer->callbackArgument,
        &temperature, sizeof(temperature));
  }
}
/*----------------------------------------------------------------------------*/
static inline uint32_t calcResetTimeout(const struct Timer *timer)
{
  static const uint32_t RESET_FREQ = 10;
  return (timerGetFrequency(timer) + RESET_FREQ - 1) / RESET_FREQ;
}
/*----------------------------------------------------------------------------*/
static uint16_t fetchSample(const struct SHT2X *sensor)
{
  const uint8_t * const buffer = sensor->buffer;
  const uint16_t value = ((buffer[0] << 8) | buffer[1]) & 0xFFFC;

  return value;
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct SHT2X * const sensor = object;
  bool waitForTimeout = false;

  timerDisable(sensor->timer);

  if (ifGetParam(sensor->bus, IF_STATUS, NULL) != E_OK)
  {
    sensor->state = STATE_ERROR_WAIT;
    timerSetOverflow(sensor->timer, calcResetTimeout(sensor->timer));
    waitForTimeout = true;
  }

  switch (sensor->state)
  {
    case STATE_CONFIG_WRITE_WAIT:
      sensor->state = STATE_IDLE;
      atomicFetchAnd(&sensor->flags, ~FLAG_RESET);
      atomicFetchOr(&sensor->flags, FLAG_READY);
      break;

    case STATE_H_START_WAIT:
      sensor->state = STATE_H_WAIT;
      timerSetOverflow(sensor->timer, resolutionToHumidityTime(sensor));
      waitForTimeout = true;
      break;

    case STATE_H_READ_WAIT:
      sensor->state = STATE_T_START;
      sensor->humidity = fetchSample(sensor);
      break;

    case STATE_T_START_WAIT:
      sensor->state = STATE_T_WAIT;
      timerSetOverflow(sensor->timer, resolutionToTemperatureTime(sensor));
      waitForTimeout = true;
      break;

    case STATE_T_READ_WAIT:
      sensor->state = STATE_PROCESS;
      sensor->temperature = fetchSample(sensor);
      break;

    default:
      break;
  }

  if (waitForTimeout)
  {
    /* Switching between different overflow values requires counter reset */
    timerSetValue(sensor->timer, 0);
    timerEnable(sensor->timer);
  }

  ifSetCallback(sensor->bus, NULL, NULL);
  ifSetParam(sensor->bus, IF_RELEASE, NULL);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct SHT2X * const sensor = object;

  switch (sensor->state)
  {
    case STATE_H_WAIT:
      sensor->state = STATE_H_READ;
      break;

    case STATE_T_WAIT:
      sensor->state = STATE_T_READ;
      break;

    case STATE_ERROR_WAIT:
      sensor->state = STATE_ERROR_INTERFACE;
      break;

    default:
      ifSetCallback(sensor->bus, NULL, NULL);
      ifSetParam(sensor->bus, IF_RELEASE, NULL);
      sensor->state = STATE_ERROR_TIMEOUT;
      break;
  }

  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static uint8_t resolutionToConfig(const struct SHT2X *sensor)
{
  uint8_t config = 0x02;

  switch (sensor->resolution)
  {
    case SHT2X_RESOLUTION_8BIT:
      config |= BIT(0);
      break;

    case SHT2X_RESOLUTION_10BIT:
      config |= BIT(7);
      break;

    case SHT2X_RESOLUTION_11BIT:
      config |= BIT(7) | BIT(0);
      break;

    default:
      break;
  }

  return config;
}
/*----------------------------------------------------------------------------*/
static uint32_t resolutionToHumidityTime(const struct SHT2X *sensor)
{
  const uint32_t frequency = timerGetFrequency(sensor->timer);
  uint64_t overflow;

  switch (sensor->resolution)
  {
    case SHT2X_RESOLUTION_8BIT:
      /* 4 ms */
      overflow = frequency * (4 * (1ULL << 32) / 1000);
      break;

    case SHT2X_RESOLUTION_10BIT:
      /* 9 ms */
      overflow = frequency * (9 * (1ULL << 32) / 1000);
      break;

    case SHT2X_RESOLUTION_11BIT:
      /* 15 ms */
      overflow = frequency * (15 * (1ULL << 32) / 1000);
      break;

    default:
      /* Default overflow period is 29 ms */
      overflow = frequency * (29 * (1ULL << 32) / 1000);
      break;
  }

  return (overflow + ((1ULL << 32) - 1)) >> 32;
}
/*----------------------------------------------------------------------------*/
static uint32_t resolutionToTemperatureTime(const struct SHT2X *sensor)
{
  const uint32_t frequency = timerGetFrequency(sensor->timer);
  uint64_t overflow;

  switch (sensor->resolution)
  {
    case SHT2X_RESOLUTION_8BIT:
      /* Temperature resolution is 12 bit: 22 ms */
      overflow = frequency * (22 * (1ULL << 32) / 1000);
      break;

    case SHT2X_RESOLUTION_10BIT:
      /* Temperature resolution is 13 bit: 43 ms */
      overflow = frequency * (43 * (1ULL << 32) / 1000);
      break;

    case SHT2X_RESOLUTION_11BIT:
      /* Temperature resolution is 11 bit: 11 ms */
      overflow = frequency * (11 * (1ULL << 32) / 1000);
      break;

    default:
      /* Default temperature resolution is 14 bit: 85 ms */
      overflow = frequency * (85 * (1ULL << 32) / 1000);
      break;
  }

  return (overflow + ((1ULL << 32) - 1)) >> 32;
}
/*----------------------------------------------------------------------------*/
static void startConfigWrite(struct SHT2X *sensor)
{
  sensor->buffer[0] = CMD_WRITE_USER_REG;
  sensor->buffer[1] = resolutionToConfig(sensor);

  busInit(sensor);
  ifWrite(sensor->bus, sensor->buffer, LENGTH_CONFIG);
}
/*----------------------------------------------------------------------------*/
static void startHumidityConversion(struct SHT2X *sensor)
{
  sensor->buffer[0] = CMD_TRIGGER_RH;

  busInit(sensor);
  ifWrite(sensor->bus, sensor->buffer, LENGTH_COMMAND);
}
/*----------------------------------------------------------------------------*/
static void startSampleRead(struct SHT2X *sensor)
{
  busInit(sensor);
  ifRead(sensor->bus, sensor->buffer, sizeof(sensor->buffer));
}
/*----------------------------------------------------------------------------*/
static void startTemperatureConversion(struct SHT2X *sensor)
{
  sensor->buffer[0] = CMD_TRIGGER_T;

  busInit(sensor);
  ifWrite(sensor->bus, sensor->buffer, LENGTH_COMMAND);
}
/*----------------------------------------------------------------------------*/
static enum Result shtInit(void *object, const void *configBase)
{
  const struct SHT2XConfig * const config = configBase;
  assert(config != NULL);
  assert(config->bus != NULL && config->timer != NULL);

  struct SHT2X * const sensor = object;

  sensor->callbackArgument = NULL;
  sensor->onErrorCallback = NULL;
  sensor->onResultCallback = NULL;
  sensor->onUpdateCallback = NULL;

  sensor->thermometer = NULL;
  sensor->bus = config->bus;
  sensor->timer = config->timer;
  sensor->address = config->address;
  sensor->rate = config->rate;

  sensor->humidity = 0;
  sensor->temperature = 0;
  sensor->flags = 0;
  sensor->state = STATE_IDLE;

  if (config->resolution != SHT2X_RESOLUTION_DEFAULT)
    sensor->resolution = (uint8_t)config->resolution;
  else
    sensor->resolution = SHT2X_RESOLUTION_12BIT;

  timerSetAutostop(sensor->timer, true);
  timerSetCallback(sensor->timer, onTimerEvent, sensor);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void shtDeinit(void *object)
{
  struct SHT2X * const sensor = object;

  timerDisable(sensor->timer);
  timerSetCallback(sensor->timer, NULL, NULL);

  if (sensor->thermometer != NULL)
    deinit(sensor->thermometer);
}
/*----------------------------------------------------------------------------*/
static const char *shtGetFormat(const void *)
{
  return "i8q8";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus shtGetStatus(const void *object)
{
  const struct SHT2X * const sensor = object;

  if (atomicLoad(&sensor->flags) & FLAG_READY)
  {
    if (sensor->state == STATE_IDLE)
      return SENSOR_IDLE;
    else
      return SENSOR_BUSY;
  }
  else
    return SENSOR_ERROR;
}
/*----------------------------------------------------------------------------*/
static void shtSetCallbackArgument(void *object, void *argument)
{
  struct SHT2X * const sensor = object;
  sensor->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
static void shtSetErrorCallback(void *object,
    void (*callback)(void *, enum SensorResult))
{
  struct SHT2X * const sensor = object;
  sensor->onErrorCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void shtSetResultCallback(void *object,
    void (*callback)(void *, const void *, size_t))
{
  struct SHT2X * const sensor = object;
  sensor->onResultCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void shtSetUpdateCallback(void *object, void (*callback)(void *))
{
  struct SHT2X * const sensor = object;
  sensor->onUpdateCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void shtReset(void *object)
{
  struct SHT2X * const sensor = object;

  atomicFetchOr(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void shtSample(void *object)
{
  struct SHT2X * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->flags, FLAG_SAMPLE);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void shtStart(void *object)
{
  struct SHT2X * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->flags, FLAG_LOOP);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void shtStop(void *object)
{
  struct SHT2X * const sensor = object;

  atomicFetchAnd(&sensor->flags, ~(FLAG_RESET | FLAG_LOOP | FLAG_SAMPLE));
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void shtSuspend(void *object)
{
  struct SHT2X * const sensor = object;

  /* Clear all flags except for reset flag */
  atomicFetchAnd(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static bool shtUpdate(void *object)
{
  struct SHT2X * const sensor = object;
  bool busy;
  bool updated;

  do
  {
    busy = false;
    updated = false;

    switch ((enum State)sensor->state)
    {
      case STATE_IDLE:
      {
        const uint8_t flags = atomicLoad(&sensor->flags);

        if (flags & FLAG_RESET)
        {
          sensor->state = STATE_CONFIG_WRITE;
          updated = true;
        }
        else if (flags & (FLAG_LOOP | FLAG_SAMPLE))
        {
          if (flags & FLAG_READY)
          {
            sensor->state = STATE_H_START;
            updated = true;
          }
        }
        break;
      }

      case STATE_CONFIG_WRITE:
        sensor->state = STATE_CONFIG_WRITE_WAIT;
        atomicFetchAnd(&sensor->flags, ~FLAG_READY);
        startConfigWrite(sensor);
        busy = true;
        break;

      case STATE_CONFIG_WRITE_WAIT:
        busy = true;
        break;

      case STATE_H_START:
        sensor->state = STATE_H_START_WAIT;
        startHumidityConversion(sensor);
        busy = true;
        break;

      case STATE_H_START_WAIT:
        busy = true;
        break;

      case STATE_H_WAIT:
        break;

      case STATE_H_READ:
        sensor->state = STATE_H_READ_WAIT;
        startSampleRead(sensor);
        busy = true;
        break;

      case STATE_H_READ_WAIT:
        busy = true;
        break;

      case STATE_T_START:
        sensor->state = STATE_T_START_WAIT;
        startTemperatureConversion(sensor);
        busy = true;
        break;

      case STATE_T_START_WAIT:
        busy = true;
        break;

      case STATE_T_WAIT:
        break;

      case STATE_T_READ:
        sensor->state = STATE_T_READ_WAIT;
        startSampleRead(sensor);
        busy = true;
        break;

      case STATE_T_READ_WAIT:
        busy = true;
        break;

      case STATE_PROCESS:
        calcHumidity(sensor);

        sensor->state = STATE_IDLE;
        atomicFetchAnd(&sensor->flags, ~(FLAG_SAMPLE | FLAG_THERMO_SAMPLE));

        updated = true;
        break;

      case STATE_ERROR_WAIT:
        break;

      case STATE_ERROR_INTERFACE:
      case STATE_ERROR_TIMEOUT:
        if (sensor->onErrorCallback != NULL)
        {
          sensor->onErrorCallback(sensor->callbackArgument,
              sensor->state == STATE_ERROR_INTERFACE ?
                  SENSOR_INTERFACE_ERROR : SENSOR_INTERFACE_TIMEOUT);
        }

        sensor->state = STATE_IDLE;
        updated = true;
        break;
    }
  }
  while (updated);

  return busy;
}
/*----------------------------------------------------------------------------*/
struct SHT2XThermometer *sht2xMakeThermometer(struct SHT2X *sensor)
{
  if (sensor->thermometer == NULL)
  {
    const struct SHT2XThermometerConfig config = {
        .parent = sensor
    };

    sensor->thermometer = init(SHT2XThermometer, &config);
  }

  return sensor->thermometer;
}
