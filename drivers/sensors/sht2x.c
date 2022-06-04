/*
 * sht2x.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/sht2x.h>
#include <halm/timer.h>
#include <xcore/bits.h>
#include <xcore/interface.h>
#include <assert.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
#define LENGTH_COMMAND  1
#define LENGTH_CONFIG   2

enum Command
{
  CMD_TRIGGER_T_HOLD  = 0xE3,
  CMD_TRIGGER_RH_HOLD = 0xE5,
  CMD_TRIGGER_T       = 0xF3,
  CMD_TRIGGER_RH      = 0xF5,
  CMD_WRITE_USER_REG  = 0xE6,
  CMD_READ_USER_REG   = 0xE7,
  CMD_SOFT_RESET      = 0xFE
};

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

  STATE_PROCESS
};
/*----------------------------------------------------------------------------*/
static void busInit(struct SHT2X *);
static void calcHumidity(struct SHT2X *);
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
    .update = shtUpdate
};
/*----------------------------------------------------------------------------*/
static void busInit(struct SHT2X *sensor)
{
  /* Lock the interface */
  ifSetParam(sensor->bus, IF_ACQUIRE, 0);

  ifSetParam(sensor->bus, IF_ADDRESS, &sensor->address);
  ifSetParam(sensor->bus, IF_ZEROCOPY, 0);
  ifSetCallback(sensor->bus, onBusEvent, sensor);

  if (sensor->rate)
    ifSetParam(sensor->bus, IF_RATE, &sensor->rate);
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

  if (sensor->thermometer && sensor->thermometer->enabled)
  {
    sensor->thermometer->onResultCallback(sensor->thermometer->callbackArgument,
        &temperature, sizeof(temperature));
  }
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

  switch (sensor->state)
  {
    case STATE_CONFIG_WRITE_WAIT:
      sensor->state = STATE_IDLE;
      break;

    case STATE_H_START_WAIT:
      sensor->state = STATE_H_WAIT;
      /* TODO Switching between diffrent overflow values */
      timerSetOverflow(sensor->timer, resolutionToHumidityTime(sensor));
      timerSetValue(sensor->timer, 0);
      timerEnable(sensor->timer);
      break;

    case STATE_H_READ_WAIT:
      sensor->humidity = fetchSample(sensor);
      sensor->state = STATE_T_START;
      break;

    case STATE_T_START_WAIT:
      sensor->state = STATE_T_WAIT;
      timerSetOverflow(sensor->timer, resolutionToTemperatureTime(sensor));
      timerSetValue(sensor->timer, 0);
      timerEnable(sensor->timer);
      break;

    case STATE_T_READ_WAIT:
      sensor->temperature = fetchSample(sensor);
      sensor->state = STATE_PROCESS;
      break;

    default:
      break;
  }

  ifSetParam(sensor->bus, IF_RELEASE, 0);
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

    default:
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
  /* TODO Optimize */
  const uint64_t frequency = (uint64_t)timerGetFrequency(sensor->timer);
  uint32_t overflow;

  switch (sensor->resolution)
  {
    case SHT2X_RESOLUTION_8BIT:
      /* 4 ms */
      overflow = (frequency + 249) / 250;
      break;

    case SHT2X_RESOLUTION_10BIT:
      /* 9 ms */
      overflow = (frequency * 9 + 999) / 1000;
      break;

    case SHT2X_RESOLUTION_11BIT:
      /* 15 ms */
      overflow = (frequency * 15 + 999) / 1000;
      break;

    default:
      /* Default overflow period is 29 ms */
      overflow = (frequency * 29 + 999) / 1000;
      break;
  }

  return overflow;
}
/*----------------------------------------------------------------------------*/
static uint32_t resolutionToTemperatureTime(const struct SHT2X *sensor)
{
  /* TODO Optimize */
  const uint64_t frequency = (uint64_t)timerGetFrequency(sensor->timer);
  uint32_t overflow;

  switch (sensor->resolution)
  {
    case SHT2X_RESOLUTION_8BIT:
      /* Temperature resolution is 12 bit: 22 ms */
      overflow = (frequency * 22 + 999) / 1000;
      break;

    case SHT2X_RESOLUTION_10BIT:
      /* Temperature resolution is 13 bit: 43 ms */
      overflow = (frequency * 43 + 999) / 1000;
      break;

    case SHT2X_RESOLUTION_11BIT:
      /* Temperature resolution is 11 bit: 11 ms */
      overflow = (frequency * 11 + 999) / 1000;
      break;

    default:
      /* Default temperature resolution is 14 bit: 85 ms */
      overflow = (frequency * 85 + 999) / 1000;
      break;
  }

  return overflow;
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
  assert(config);
  assert(config->bus);
  assert(config->timer);

  struct SHT2X * const sensor = object;

  sensor->thermometer = 0;
  sensor->bus = config->bus;
  sensor->timer = config->timer;
  sensor->rate = config->rate;
  sensor->address = config->address;

  sensor->callbackArgument = 0;
  sensor->onErrorCallback = 0;
  sensor->onResultCallback = 0;
  sensor->onUpdateCallback = 0;

  sensor->humidity = 0;
  sensor->temperature = 0;
  sensor->state = STATE_IDLE;

  sensor->reset = false;
  sensor->start = false;
  sensor->stop = false;

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
  timerSetCallback(sensor->timer, 0, 0);

  if (sensor->thermometer)
    deinit(sensor->thermometer);
}
/*----------------------------------------------------------------------------*/
static const char *shtGetFormat(const void *object __attribute__((unused)))
{
  return "i8q8";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus shtGetStatus(const void *object)
{
  const struct SHT2X * const sensor = object;

  if (sensor->state == STATE_IDLE)
    return SENSOR_IDLE;
  else
    return SENSOR_BUSY;
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

  sensor->reset = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void shtSample(void *object)
{
  struct SHT2X * const sensor = object;

  assert(sensor->onResultCallback);
  assert(sensor->onUpdateCallback);

  sensor->start = true;
  sensor->stop = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void shtStart(void *object)
{
  struct SHT2X * const sensor = object;

  assert(sensor->onResultCallback);
  assert(sensor->onUpdateCallback);

  sensor->start = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void shtStop(void *object)
{
  struct SHT2X * const sensor = object;

  sensor->stop = true;
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
          sensor->start = false;
          sensor->state = STATE_H_START;
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
          sensor->state = STATE_H_START;
          updated = true;
        }
        break;
    }
  }
  while (updated);

  return busy;
}
/*----------------------------------------------------------------------------*/
struct SHT2XThermometer *sht2xMakeThermometer(struct SHT2X *sensor)
{
  if (!sensor->thermometer)
    sensor->thermometer = init(SHT2XThermometer, 0);

  return sensor->thermometer;
}
