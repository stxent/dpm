/*
 * ds18b20.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/ds18b20.h>
#include <halm/timer.h>
#include <xcore/atomic.h>
#include <xcore/crc/crc8_dallas.h>
#include <xcore/interface.h>
#include <assert.h>
#include <stdbool.h>
/*----------------------------------------------------------------------------*/
#define LENGTH_CONFIG 4

enum
{
  FLAG_RESET  = 0x01,
  FLAG_READY  = 0x02,
  FLAG_LOOP   = 0x04,
  FLAG_SAMPLE = 0x08
};

enum State
{
  STATE_IDLE,
  STATE_CONFIG_WRITE,
  STATE_CONFIG_WRITE_WAIT,
  STATE_TEMP_CONVERSION,
  STATE_TEMP_CONVERSION_WAIT,
  STATE_TEMP_WAIT_START,
  STATE_TEMP_WAIT,
  STATE_TEMP_REQUEST,
  STATE_TEMP_REQUEST_WAIT,
  STATE_TEMP_READ,
  STATE_TEMP_READ_WAIT,
  STATE_PROCESS
};
/*----------------------------------------------------------------------------*/
static void busInit(struct DS18B20 *);
static void calcTemperature(void *);
static int32_t makeSampleValue(struct DS18B20 *);
static void onBusEvent(void *);
static void onTimerEvent(void *);
static inline uint8_t resolutionToConfig(const struct DS18B20 *);
static inline uint32_t resolutionToTime(const struct DS18B20 *);
static void startConfigWrite(struct DS18B20 *);
static void startTemperatureConversion(struct DS18B20 *);
static void startTemperatureRead(struct DS18B20 *);
static void startTemperatureRequest(struct DS18B20 *);

static enum Result dsInit(void *, const void *);
static void dsDeinit(void *);
static const char *dsGetFormat(const void *);
static enum SensorStatus dsGetStatus(const void *);
static void dsSetCallbackArgument(void *, void *);
static void dsSetErrorCallback(void *, void (*)(void *, enum SensorResult));
static void dsSetResultCallback(void *,
    void (*)(void *, const void *, size_t));
static void dsSetUpdateCallback(void *, void (*)(void *));
static void dsReset(void *);
static void dsSample(void *);
static void dsStart(void *);
static void dsStop(void *);
static void dsSuspend(void *);
static bool dsUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct SensorClass * const DS18B20 = &(const struct SensorClass){
    .size = sizeof(struct DS18B20),
    .init = dsInit,
    .deinit = dsDeinit,

    .getFormat = dsGetFormat,
    .getStatus = dsGetStatus,
    .setCallbackArgument = dsSetCallbackArgument,
    .setErrorCallback = dsSetErrorCallback,
    .setResultCallback = dsSetResultCallback,
    .setUpdateCallback = dsSetUpdateCallback,
    .reset = dsReset,
    .sample = dsSample,
    .start = dsStart,
    .stop = dsStop,
    .suspend = dsSuspend,
    .update = dsUpdate
};
/*----------------------------------------------------------------------------*/
static const uint8_t readScratchpadCommand[]  = {0xBE};
static const uint8_t startConversionCommand[] = {0x44};
static const uint8_t writeScratchpadCommand[] = {0x4E};
/*----------------------------------------------------------------------------*/
static void busInit(struct DS18B20 *sensor)
{
  /* Lock the interface */
  ifSetParam(sensor->bus, IF_ACQUIRE, NULL);

  ifSetParam(sensor->bus, IF_ADDRESS_64, &sensor->address);
  ifSetParam(sensor->bus, IF_ZEROCOPY, NULL);
  ifSetCallback(sensor->bus, onBusEvent, sensor);
}
/*----------------------------------------------------------------------------*/
static void calcTemperature(void *object)
{
  struct DS18B20 * const sensor = object;

  const uint8_t checksum = crc8DallasUpdate(0x00,
      sensor->scratchpad, sizeof(sensor->scratchpad) - 1);

  if (checksum == sensor->scratchpad[8])
  {
    const int32_t result = makeSampleValue(sensor);
    sensor->onResultCallback(sensor->callbackArgument, &result, sizeof(result));
  }
  else
  {
    if (sensor->onErrorCallback != NULL)
      sensor->onErrorCallback(sensor->callbackArgument, SENSOR_DATA_ERROR);
  }
}
/*----------------------------------------------------------------------------*/
static int32_t makeSampleValue(struct DS18B20 *sensor)
{
  /* Process received buffer */
  const uint8_t * const buffer = sensor->scratchpad;
  const uint16_t value = (buffer[1] << 8) | buffer[0];

  return (int32_t)((int16_t)value * 16);
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct DS18B20 * const sensor = object;
  bool release = true;

  switch (sensor->state)
  {
    case STATE_CONFIG_WRITE_WAIT:
      atomicFetchAnd(&sensor->flags, ~FLAG_RESET);
      atomicFetchOr(&sensor->flags, FLAG_READY);
      sensor->state = STATE_IDLE;
      break;

    case STATE_TEMP_CONVERSION_WAIT:
      sensor->state = STATE_TEMP_WAIT_START;
      break;

    case STATE_TEMP_REQUEST_WAIT:
      sensor->state = STATE_TEMP_READ;
      release = false;
      break;

    case STATE_TEMP_READ_WAIT:
      sensor->state = STATE_PROCESS;
      break;

    default:
      break;
  }

  if (release)
  {
    ifSetCallback(sensor->bus, NULL, NULL);
    ifSetParam(sensor->bus, IF_RELEASE, NULL);
  }

  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct DS18B20 * const sensor = object;

  sensor->state = STATE_TEMP_REQUEST;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static inline uint8_t resolutionToConfig(const struct DS18B20 *sensor)
{
  uint8_t config = 0x7F;

  switch (sensor->resolution)
  {
    case DS18B20_RESOLUTION_9BIT:
      config = 0x1F;
      break;

    case DS18B20_RESOLUTION_10BIT:
      config = 0x3F;
      break;

    case DS18B20_RESOLUTION_11BIT:
      config = 0x5F;
      break;

    default:
      break;
  }

  return config;
}
/*----------------------------------------------------------------------------*/
static inline uint32_t resolutionToTime(const struct DS18B20 *sensor)
{
  const uint32_t frequency = timerGetFrequency(sensor->timer);
  uint64_t overflow;

  switch (sensor->resolution)
  {
    case DS18B20_RESOLUTION_9BIT:
      /* 93.75 ms */
      overflow = frequency * ((9375 * (1ULL << 32)) / 100000);
      break;

    case DS18B20_RESOLUTION_10BIT:
      /* 187.5 ms */
      overflow = frequency * ((18750 * (1ULL << 32)) / 100000);
      break;

    case DS18B20_RESOLUTION_11BIT:
      /* 375 ms */
      overflow = frequency * ((37500 * (1ULL << 32)) / 100000);
      break;

    default:
      /* Default overflow period is 750 ms */
      overflow = frequency * ((75000 * (1ULL << 32)) / 100000);
      break;
  }

  return (overflow + ((1ULL << 32) - 1)) >> 32;
}
/*----------------------------------------------------------------------------*/
static void startConfigWrite(struct DS18B20 *sensor)
{
  sensor->scratchpad[0] = writeScratchpadCommand[0];
  sensor->scratchpad[1] = -55;
  sensor->scratchpad[2] = 125;
  sensor->scratchpad[3] = resolutionToConfig(sensor);

  busInit(sensor);
  ifWrite(sensor->bus, sensor->scratchpad, LENGTH_CONFIG);
}
/*----------------------------------------------------------------------------*/
static void startTemperatureConversion(struct DS18B20 *sensor)
{
  busInit(sensor);
  ifWrite(sensor->bus, startConversionCommand, sizeof(startConversionCommand));
}
/*----------------------------------------------------------------------------*/
static void startTemperatureRead(struct DS18B20 *sensor)
{
  /* Continue interface read */
  ifRead(sensor->bus, sensor->scratchpad, sizeof(sensor->scratchpad));
}
/*----------------------------------------------------------------------------*/
static void startTemperatureRequest(struct DS18B20 *sensor)
{
  busInit(sensor);
  ifWrite(sensor->bus, readScratchpadCommand, sizeof(readScratchpadCommand));
}
/*----------------------------------------------------------------------------*/
static enum Result dsInit(void *object, const void *configBase)
{
  const struct DS18B20Config * const config = configBase;
  assert(config != NULL);
  assert(config->bus != NULL && config->timer != NULL);

  struct DS18B20 * const sensor = object;

  sensor->callbackArgument = NULL;
  sensor->onErrorCallback = NULL;
  sensor->onResultCallback = NULL;
  sensor->onUpdateCallback = NULL;

  sensor->address = config->address;
  sensor->bus = config->bus;
  sensor->timer = config->timer;

  sensor->flags = 0;
  sensor->state = STATE_IDLE;

  if (config->resolution != DS18B20_RESOLUTION_DEFAULT)
    sensor->resolution = (uint8_t)config->resolution;
  else
    sensor->resolution = DS18B20_RESOLUTION_12BIT;

  timerSetAutostop(sensor->timer, true);
  timerSetCallback(sensor->timer, onTimerEvent, sensor);
  timerSetOverflow(sensor->timer, resolutionToTime(sensor));

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void dsDeinit(void *object)
{
  struct DS18B20 * const sensor = object;

  timerDisable(sensor->timer);
  timerSetCallback(sensor->timer, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
static const char *dsGetFormat(const void *)
{
  return "i24q8";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus dsGetStatus(const void *object)
{
  const struct DS18B20 * const sensor = object;
  return sensor->state == STATE_IDLE ? SENSOR_IDLE : SENSOR_BUSY;
}
/*----------------------------------------------------------------------------*/
static void dsSetCallbackArgument(void *object, void *argument)
{
  struct DS18B20 * const sensor = object;
  sensor->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
static void dsSetErrorCallback(void *object,
    void (*callback)(void *, enum SensorResult))
{
  struct DS18B20 * const sensor = object;
  sensor->onErrorCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void dsSetResultCallback(void *object,
    void (*callback)(void *, const void *, size_t))
{
  struct DS18B20 * const sensor = object;
  sensor->onResultCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void dsSetUpdateCallback(void *object, void (*callback)(void *))
{
  struct DS18B20 * const sensor = object;
  sensor->onUpdateCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void dsReset(void *object)
{
  struct DS18B20 * const sensor = object;

  atomicFetchOr(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void dsSample(void *object)
{
  struct DS18B20 * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->flags, FLAG_SAMPLE);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void dsStart(void *object)
{
  struct DS18B20 * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->flags, FLAG_LOOP);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void dsStop(void *object)
{
  struct DS18B20 * const sensor = object;

  atomicFetchAnd(&sensor->flags, ~(FLAG_RESET | FLAG_LOOP | FLAG_SAMPLE));
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void dsSuspend(void *object)
{
  struct DS18B20 * const sensor = object;

  /* Clear all flags except for reset flag */
  atomicFetchAnd(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static bool dsUpdate(void *object)
{
  struct DS18B20 * const sensor = object;
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
            sensor->state = STATE_TEMP_CONVERSION;
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

      case STATE_TEMP_CONVERSION:
        sensor->state = STATE_TEMP_CONVERSION_WAIT;
        startTemperatureConversion(sensor);
        busy = true;
        break;

      case STATE_TEMP_CONVERSION_WAIT:
        busy = true;
        break;

      case STATE_TEMP_WAIT_START:
        sensor->state = STATE_TEMP_WAIT;
        timerEnable(sensor->timer);
        break;

      case STATE_TEMP_WAIT:
        break;

      case STATE_TEMP_REQUEST:
        sensor->state = STATE_TEMP_REQUEST_WAIT;
        startTemperatureRequest(sensor);
        busy = true;
        break;

      case STATE_TEMP_REQUEST_WAIT:
        busy = true;
        break;

      case STATE_TEMP_READ:
        sensor->state = STATE_TEMP_READ_WAIT;
        startTemperatureRead(sensor);
        busy = true;
        break;

      case STATE_TEMP_READ_WAIT:
        busy = true;
        break;

      case STATE_PROCESS:
        calcTemperature(sensor);

        sensor->state = STATE_IDLE;
        atomicFetchAnd(&sensor->flags, ~FLAG_SAMPLE);

        updated = true;
        break;
    }
  }
  while (updated);

  return busy;
}
