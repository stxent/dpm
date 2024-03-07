/*
 * hmc5883.c
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/hmc5883.h>
#include <dpm/sensors/hmc5883_defs.h>
#include <halm/generic/i2c.h>
#include <halm/interrupt.h>
#include <halm/timer.h>
#include <xcore/atomic.h>
#include <xcore/bits.h>
#include <xcore/interface.h>
#include <assert.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
#define LENGTH_CONFIG 3

enum State
{
  STATE_IDLE,

  STATE_CONFIG_WRITE,
  STATE_CONFIG_WRITE_WAIT,
  STATE_CONFIG_END,

  STATE_SUSPEND_START,
  STATE_SUSPEND_BUS_WAIT,
  STATE_SUSPEND_END,

  STATE_EVENT_WAIT,
  STATE_REQUEST,
  STATE_REQUEST_WAIT,
  STATE_READ,
  STATE_READ_WAIT,

  STATE_PROCESS,

  STATE_ERROR_WAIT,
  STATE_ERROR_DEVICE,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
};
/*----------------------------------------------------------------------------*/
static void busInit(struct HMC5883 *, bool);
static inline uint32_t calcResetTimeout(const struct Timer *);
static void calcValues(struct HMC5883 *);
static int32_t gainToScale(const struct HMC5883 *);
static void makeConfig(const struct HMC5883 *, uint8_t *);
static void onBusEvent(void *);
static void onPinEvent(void *);
static void onTimerEvent(void *);
static void startConfigWrite(struct HMC5883 *);
static void startSampleRead(struct HMC5883 *);
static void startSampleRequest(struct HMC5883 *);
static void startSuspendSequence(struct HMC5883 *);

static enum Result hmcInit(void *, const void *);
static void hmcDeinit(void *);
static const char *hmcGetFormat(const void *);
static enum SensorStatus hmcGetStatus(const void *);
static void hmcSetCallbackArgument(void *, void *);
static void hmcSetErrorCallback(void *, void (*)(void *, enum SensorResult));
static void hmcSetResultCallback(void *,
    void (*)(void *, const void *, size_t));
static void hmcSetUpdateCallback(void *, void (*)(void *));
static void hmcReset(void *);
static void hmcSample(void *);
static void hmcStart(void *);
static void hmcStop(void *);
static void hmcSuspend(void *);
static bool hmcUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct SensorClass * const HMC5883 = &(const struct SensorClass){
    .size = sizeof(struct HMC5883),
    .init = hmcInit,
    .deinit = hmcDeinit,

    .getFormat = hmcGetFormat,
    .getStatus = hmcGetStatus,
    .setCallbackArgument = hmcSetCallbackArgument,
    .setErrorCallback = hmcSetErrorCallback,
    .setResultCallback = hmcSetResultCallback,
    .setUpdateCallback = hmcSetUpdateCallback,
    .reset = hmcReset,
    .sample = hmcSample,
    .start = hmcStart,
    .stop = hmcStop,
    .suspend = hmcSuspend,
    .update = hmcUpdate
};
/*----------------------------------------------------------------------------*/
static void busInit(struct HMC5883 *sensor, bool read)
{
  /* Lock the interface */
  ifSetParam(sensor->bus, IF_ACQUIRE, NULL);

  ifSetParam(sensor->bus, IF_ZEROCOPY, NULL);
  ifSetCallback(sensor->bus, onBusEvent, sensor);

  if (sensor->rate)
    ifSetParam(sensor->bus, IF_RATE, &sensor->rate);

  /* I2C bus */
  ifSetParam(sensor->bus, IF_ADDRESS, &sensor->address);

  if (read)
    ifSetParam(sensor->bus, IF_I2C_REPEATED_START, NULL);

  /* Start bus watchdog */
  timerSetOverflow(sensor->timer, calcResetTimeout(sensor->timer));
  timerSetValue(sensor->timer, 0);
  timerEnable(sensor->timer);
}
/*----------------------------------------------------------------------------*/
static inline uint32_t calcResetTimeout(const struct Timer *timer)
{
  static const uint32_t RESET_FREQ = 10;
  return (timerGetFrequency(timer) + RESET_FREQ - 1) / RESET_FREQ;
}
/*----------------------------------------------------------------------------*/
static void calcValues(struct HMC5883 *sensor)
{
  const int32_t scale = gainToScale(sensor);
  int32_t magnitude[3];

  magnitude[0] = (int16_t)((sensor->buffer[0] << 8) | sensor->buffer[1]);
  magnitude[1] = (int16_t)((sensor->buffer[4] << 8) | sensor->buffer[5]);
  magnitude[2] = (int16_t)((sensor->buffer[2] << 8) | sensor->buffer[3]);

  /* Convert from raw data to temporary form of i8q24 and then to i16q16 */
  magnitude[0] = (magnitude[0] * scale) >> 8;
  magnitude[1] = (magnitude[1] * scale) >> 8;
  magnitude[2] = (magnitude[2] * scale) >> 8;

  sensor->onResultCallback(sensor->callbackArgument,
      magnitude, sizeof(magnitude));
}
/*----------------------------------------------------------------------------*/
static int32_t gainToScale(const struct HMC5883 *sensor)
{
  static const int32_t SCALE[] = {
      [GN_0_88_GA] = 12246, /* 1370 LSB / Gauss */
      [GN_1_3_GA] = 15392, /* 1090 LSB / Gauss */
      [GN_1_9_GA] = 20460, /* 820 LSB / Gauss */
      [GN_2_5_GA] = 25420, /* 660 LSB / Gauss */
      [GN_4_0_GA] = 38130, /* 440 LSB / Gauss */
      [GN_4_7_GA] = 43019, /* 390 LSB / Gauss */
      [GN_5_6_GA] = 50840, /* 330 LSB / Gauss */
      [GN_8_1_GA] = 72944 /* 230 LSB / Gauss */
  };

  return SCALE[sensor->gain];
}
/*----------------------------------------------------------------------------*/
static void makeConfig(const struct HMC5883 *sensor, uint8_t *config)
{
  uint8_t configA = CONFIG_A_DO(sensor->frequency);
  const uint8_t configB = CONFIG_B_GN(sensor->gain);
  const uint8_t mode = MODE_MD(MD_CONTINUOUS);

  if (sensor->calibration == CAL_NEG_OFFSET)
    configA |= CONFIG_A_MS(MS_NEGATIVE_BIAS);
  else if (sensor->calibration == CAL_POS_OFFSET)
    configA |= CONFIG_A_MS(MS_POSITIVE_BIAS);
  else
    configA |= CONFIG_A_MS(MS_NORMAL) | CONFIG_A_MA(sensor->oversampling);

  config[0] = configA;
  config[1] = configB;
  config[2] = mode;
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct HMC5883 * const sensor = object;
  bool release = true;

  timerDisable(sensor->timer);

  if (ifGetParam(sensor->bus, IF_STATUS, NULL) != E_OK)
  {
    /* I2C bus */
    sensor->state = STATE_ERROR_WAIT;

    timerSetOverflow(sensor->timer, calcResetTimeout(sensor->timer));
    timerSetValue(sensor->timer, 0);
    timerEnable(sensor->timer);
  }

  switch (sensor->state)
  {
    case STATE_CONFIG_WRITE_WAIT:
      sensor->state = STATE_CONFIG_END;
      break;

    case STATE_SUSPEND_BUS_WAIT:
      sensor->state = STATE_SUSPEND_END;
      break;

    case STATE_REQUEST_WAIT:
      sensor->state = STATE_READ;
      release = false;
      break;

    case STATE_READ_WAIT:
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
static void onPinEvent(void *object)
{
  struct HMC5883 * const sensor = object;

  atomicFetchOr(&sensor->flags, FLAG_EVENT);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct HMC5883 * const sensor = object;

  switch (sensor->state)
  {
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
static void startConfigWrite(struct HMC5883 *sensor)
{
  sensor->buffer[0] = REG_CONFIG_A;
  makeConfig(sensor, sensor->buffer + 1);

  busInit(sensor, false);
  ifWrite(sensor->bus, sensor->buffer, 1 + LENGTH_CONFIG);
}
/*----------------------------------------------------------------------------*/
static void startSampleRead(struct HMC5883 *sensor)
{
  ifRead(sensor->bus, sensor->buffer, sizeof(sensor->buffer));
}
/*----------------------------------------------------------------------------*/
static void startSampleRequest(struct HMC5883 *sensor)
{
  sensor->buffer[0] = REG_DATA_X_MSB;

  busInit(sensor, true);
  ifWrite(sensor->bus, sensor->buffer, 1);
}
/*----------------------------------------------------------------------------*/
static void startSuspendSequence(struct HMC5883 *sensor)
{
  sensor->buffer[0] = REG_MODE;
  sensor->buffer[1] = MODE_MD(MD_IDLE);

  busInit(sensor, false);
  ifWrite(sensor->bus, sensor->buffer, 2);
}
/*----------------------------------------------------------------------------*/
static enum Result hmcInit(void *object, const void *configBase)
{
  const struct HMC5883Config * const config = configBase;
  assert(config != NULL);
  assert(config->bus != NULL && config->timer != NULL);

  struct HMC5883 * const sensor = object;

  sensor->callbackArgument = NULL;
  sensor->onErrorCallback = NULL;
  sensor->onResultCallback = NULL;
  sensor->onUpdateCallback = NULL;

  sensor->bus = config->bus;
  sensor->event = config->event;
  sensor->timer = config->timer;
  sensor->address = config->address;
  sensor->rate = config->rate;

  sensor->calibration = CAL_DISABLED;
  sensor->flags = 0;
  sensor->state = STATE_IDLE;

  if (config->frequency != HMC5883_FREQUENCY_DEFAULT)
    sensor->frequency = (uint8_t)config->frequency - 1;
  else
    sensor->frequency = HMC5883_FREQUENCY_15HZ;

  if (config->gain != HMC5883_GAIN_DEFAULT)
    sensor->gain = (uint8_t)config->gain - 1;
  else
    sensor->gain = HMC5883_GAIN_1300MGA;

  if (config->oversampling != HMC5883_OVERSAMPLING_DEFAULT)
    sensor->oversampling = (uint8_t)config->oversampling - 1;
  else
    sensor->oversampling = HMC5883_OVERSAMPLING_NONE;

  interruptSetCallback(sensor->event, onPinEvent, sensor);
  timerSetAutostop(sensor->timer, true);
  timerSetCallback(sensor->timer, onTimerEvent, sensor);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void hmcDeinit(void *object)
{
  struct HMC5883 * const sensor = object;

  timerDisable(sensor->timer);
  timerSetCallback(sensor->timer, NULL, NULL);

  interruptDisable(sensor->event);
  interruptSetCallback(sensor->event, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
static const char *hmcGetFormat([[maybe_unused]] const void *object)
{
  return "i16q16i16q16i16q16";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus hmcGetStatus(const void *object)
{
  const struct HMC5883 * const sensor = object;

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
static void hmcSetCallbackArgument(void *object, void *argument)
{
  struct HMC5883 * const sensor = object;
  sensor->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
static void hmcSetErrorCallback(void *object,
    void (*callback)(void *, enum SensorResult))
{
  struct HMC5883 * const sensor = object;
  sensor->onErrorCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void hmcSetResultCallback(void *object,
    void (*callback)(void *, const void *, size_t))
{
  struct HMC5883 * const sensor = object;
  sensor->onResultCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void hmcSetUpdateCallback(void *object, void (*callback)(void *))
{
  struct HMC5883 * const sensor = object;
  sensor->onUpdateCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void hmcReset(void *object)
{
  struct HMC5883 * const sensor = object;

  atomicFetchOr(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void hmcSample(void *object)
{
  struct HMC5883 * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->flags, FLAG_SAMPLE);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void hmcStart(void *object)
{
  struct HMC5883 * const sensor = object;

  assert(sensor->onResultCallback != NULL);
  assert(sensor->onUpdateCallback != NULL);

  atomicFetchOr(&sensor->flags, FLAG_LOOP);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void hmcStop(void *object)
{
  struct HMC5883 * const sensor = object;

  atomicFetchAnd(&sensor->flags, ~(FLAG_RESET | FLAG_LOOP | FLAG_SAMPLE));
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void hmcSuspend(void *object)
{
  struct HMC5883 * const sensor = object;

  /* Clear all flags except for reset flag */
  atomicFetchAnd(&sensor->flags, FLAG_SUSPEND);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static bool hmcUpdate(void *object)
{
  struct HMC5883 * const sensor = object;
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
        else if (flags & FLAG_SUSPEND)
        {
          sensor->state = STATE_SUSPEND_START;
          updated = true;
        }
        else if (flags & (FLAG_LOOP | FLAG_SAMPLE))
        {
          if (flags & FLAG_READY)
          {
            if (flags & FLAG_LOOP)
            {
              sensor->state = STATE_EVENT_WAIT;
              interruptEnable(sensor->event);
            }
            else
            {
              sensor->state = STATE_REQUEST;
              updated = true;
            }
          }
          else
            interruptDisable(sensor->event);
        }
        else
          interruptDisable(sensor->event);

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

      case STATE_CONFIG_END:
        sensor->state = STATE_IDLE;
        atomicFetchAnd(&sensor->flags, ~(FLAG_RESET | FLAG_EVENT));
        atomicFetchOr(&sensor->flags, FLAG_READY);
        updated = true;
        break;

      case STATE_SUSPEND_START:
        interruptDisable(sensor->event);

        sensor->state = STATE_SUSPEND_BUS_WAIT;
        startSuspendSequence(sensor);
        busy = true;
        break;

      case STATE_SUSPEND_BUS_WAIT:
        busy = true;
        break;

      case STATE_SUSPEND_END:
        sensor->state = STATE_IDLE;

        /* Clear all flags except reset flag */
        atomicFetchAnd(&sensor->flags, FLAG_RESET);
        updated = true;
        break;

      case STATE_EVENT_WAIT:
      {
        const uint16_t flags = atomicLoad(&sensor->flags);

        if (flags & (FLAG_RESET | FLAG_EVENT))
        {
          if (flags & FLAG_RESET)
          {
            sensor->state = STATE_CONFIG_WRITE;
          }
          else if (flags & FLAG_SUSPEND)
          {
            sensor->state = STATE_SUSPEND_START;
          }
          else
          {
            sensor->state = STATE_REQUEST;
            atomicFetchAnd(&sensor->flags, ~FLAG_EVENT);
          }

          updated = true;
        }
        else if (!(flags & FLAG_LOOP))
        {
          sensor->state = STATE_IDLE;
          updated = true;
        }
        break;
      }

      case STATE_REQUEST:
        sensor->state = STATE_REQUEST_WAIT;
        startSampleRequest(sensor);
        busy = true;
        break;

      case STATE_REQUEST_WAIT:
        busy = true;
        break;

      case STATE_READ:
        sensor->state = STATE_READ_WAIT;
        startSampleRead(sensor);
        busy = true;
        break;

      case STATE_READ_WAIT:
        busy = true;
        break;

      case STATE_PROCESS:
        calcValues(sensor);

        sensor->state = STATE_IDLE;
        atomicFetchAnd(&sensor->flags, ~FLAG_SAMPLE);

        updated = true;
        break;

      case STATE_ERROR_WAIT:
        break;

      case STATE_ERROR_DEVICE:
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
void hmc5883ApplyNegOffset(struct HMC5883 *sensor)
{
  sensor->calibration = CAL_NEG_OFFSET;
  atomicFetchOr(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
void hmc5883ApplyPosOffset(struct HMC5883 *sensor)
{
  sensor->calibration = CAL_POS_OFFSET;
  atomicFetchOr(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
void hmc5883EnableNormalMode(struct HMC5883 *sensor)
{
  sensor->calibration = CAL_DISABLED;
  atomicFetchOr(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
