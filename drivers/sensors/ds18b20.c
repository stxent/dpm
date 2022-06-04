/*
 * ds18b20.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/ds18b20.h>
#include <halm/timer.h>
#include <xcore/crc/crc8_dallas.h>
#include <xcore/interface.h>
#include <assert.h>
#include <stdbool.h>
/*----------------------------------------------------------------------------*/
#define LENGTH_CONFIG 4

enum
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
static int16_t makeSampleValue(struct DS18B20 *);
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
  ifSetParam(sensor->bus, IF_ACQUIRE, 0);

  ifSetParam(sensor->bus, IF_ADDRESS_64, &sensor->address);
  ifSetParam(sensor->bus, IF_ZEROCOPY, 0);
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
    const int16_t result = makeSampleValue(sensor);
    sensor->onResultCallback(sensor->callbackArgument, &result, sizeof(result));
  }
  else
  {
    if (sensor->onErrorCallback)
      sensor->onErrorCallback(sensor->callbackArgument, SENSOR_DATA_ERROR);
  }
}
/*----------------------------------------------------------------------------*/
static int16_t makeSampleValue(struct DS18B20 *sensor)
{
  /* Process received buffer */
  const uint8_t * const buffer = sensor->scratchpad;
  const uint16_t value = (buffer[1] << 8) | buffer[0];

  return (int16_t)value;
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct DS18B20 * const sensor = object;

  switch (sensor->state)
  {
    case STATE_CONFIG_WRITE_WAIT:
      ifSetParam(sensor->bus, IF_RELEASE, 0);
      sensor->state = STATE_IDLE;
      break;

    case STATE_TEMP_CONVERSION_WAIT:
      ifSetParam(sensor->bus, IF_RELEASE, 0);
      sensor->state = STATE_TEMP_WAIT_START;
      break;

    case STATE_TEMP_REQUEST_WAIT:
      sensor->state = STATE_TEMP_READ;
      break;

    case STATE_TEMP_READ_WAIT:
      ifSetParam(sensor->bus, IF_RELEASE, 0);
      sensor->state = STATE_PROCESS;
      break;

    default:
      break;
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
  uint32_t overflow = (frequency * 3 + 3) / 4;

  switch (sensor->resolution)
  {
    case DS18B20_RESOLUTION_9BIT:
      /* 93.75 ms */
      overflow = (overflow + 1) >> 1;
      /* Falls through */

    case DS18B20_RESOLUTION_10BIT:
      /* 187.5 ms */
      overflow = (overflow + 1) >> 1;
      /* Falls through */

    case DS18B20_RESOLUTION_11BIT:
      /* 375 ms */
      overflow = (overflow + 1) >> 1;
      /* Falls through */
      break;

    default:
      /* Default overflow period is 750 ms */
      break;
  }

  return overflow;
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
  assert(config);
  assert(config->bus && config->timer);

  struct DS18B20 * const sensor = object;

  sensor->address = config->address;
  sensor->bus = config->bus;
  sensor->timer = config->timer;
  sensor->state = STATE_IDLE;

  sensor->callbackArgument = 0;
  sensor->onErrorCallback = 0;
  sensor->onResultCallback = 0;
  sensor->onUpdateCallback = 0;

  sensor->reset = false;
  sensor->start = false;
  sensor->stop = false;

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
  timerSetCallback(sensor->timer, 0, 0);
}
/*----------------------------------------------------------------------------*/
static const char *dsGetFormat(const void *object __attribute__((unused)))
{
  return "i12q4";
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

  sensor->reset = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void dsSample(void *object)
{
  struct DS18B20 * const sensor = object;

  assert(sensor->onResultCallback);
  assert(sensor->onUpdateCallback);

  sensor->start = true;
  sensor->stop = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void dsStart(void *object)
{
  struct DS18B20 * const sensor = object;

  assert(sensor->onResultCallback);
  assert(sensor->onUpdateCallback);

  sensor->start = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void dsStop(void *object)
{
  struct DS18B20 * const sensor = object;

  sensor->stop = true;
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

    switch (sensor->state)
    {
      case STATE_IDLE:
        if (sensor->reset)
        {
          sensor->state = STATE_CONFIG_WRITE;
          sensor->reset = false;
          updated = true;
        }
        else if (sensor->start)
        {
          sensor->state = STATE_TEMP_CONVERSION;
          sensor->start = false;
          updated = true;
        }
        else if (sensor->stop)
        {
          sensor->stop = false;
        }
        break;

      case STATE_CONFIG_WRITE:
        sensor->state = STATE_CONFIG_WRITE_WAIT;
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

        if (sensor->stop)
        {
          sensor->stop = false;
          sensor->state = STATE_IDLE;
        }
        else if (sensor->reset)
        {
          sensor->reset = false;
          sensor->start = true;
          sensor->state = STATE_CONFIG_WRITE;
          updated = true;
        }
        else
        {
          sensor->state = STATE_TEMP_CONVERSION;
          updated = true;
        }
        break;
    }
  }
  while (updated);

  return busy;
}
