/*
 * xpt2046.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/xpt2046.h>
#include <halm/generic/spi.h>
#include <halm/interrupt.h>
#include <halm/timer.h>
#include <xcore/bits.h>
#include <xcore/interface.h>
#include <assert.h>
#include <limits.h>
/*----------------------------------------------------------------------------*/
#define CTRL_ADC_ON   BIT(0)
#define CTRL_REF_ON   BIT(1)
#define CTRL_DFR      0
#define CTRL_SER      BIT(2)
#define CTRL_HI_Y     BIT_FIELD(0x09, 4)
#define CTRL_Z1_POS   BIT_FIELD(0x0B, 4)
#define CTRL_Z2_POS   BIT_FIELD(0x0C, 4)
#define CTRL_HI_X     BIT_FIELD(0x0D, 4)

#define ADC_MAX       MASK(12)
#define UPDATE_FREQ   100

enum
{
  STATE_IDLE,
  STATE_EVENT_WAIT,
  STATE_READ,
  STATE_READ_WAIT,
  STATE_PROCESS
};
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *);
static void onPinEvent(void *);
static void onTimerEvent(void *);

static enum Result tsInit(void *, const void *);
static void tsDeinit(void *);
static const char *tsGetFormat(const void *);
static enum SensorStatus tsGetStatus(const void *);
static void tsSetCallbackArgument(void *, void *);
static void tsSetErrorCallback(void *, void (*)(void *, enum SensorResult));
static void tsSetResultCallback(void *,
    void (*)(void *, const void *, size_t));
static void tsSetUpdateCallback(void *, void (*)(void *));
static void tsReset(void *);
static void tsSample(void *);
static void tsStart(void *);
static void tsStop(void *);
static bool tsUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct SensorClass * const XPT2046 = &(const struct SensorClass){
    .size = sizeof(struct XPT2046),
    .init = tsInit,
    .deinit = tsDeinit,

    .getFormat = tsGetFormat,
    .getStatus = tsGetStatus,
    .setCallbackArgument = tsSetCallbackArgument,
    .setErrorCallback = tsSetErrorCallback,
    .setResultCallback = tsSetResultCallback,
    .setUpdateCallback = tsSetUpdateCallback,
    .reset = tsReset,
    .sample = tsSample,
    .start = tsStart,
    .stop = tsStop,
    .update = tsUpdate
};
/*----------------------------------------------------------------------------*/
static void calcPosition(void *object)
{
  struct XPT2046 * const sensor = object;
  const uint16_t z1 = (sensor->rxBuffer[1] << 8 | sensor->rxBuffer[2]) >> 3;
  const uint16_t z2 = (sensor->rxBuffer[3] << 8 | sensor->rxBuffer[4]) >> 3;
  int z = (ADC_MAX - z2) + z1;

  if (z < 0)
    z = -z;

  if (z > (int)sensor->threshold)
  {
    /* Touch panel pressed */

    const uint16_t x = (sensor->rxBuffer[5] << 8 | sensor->rxBuffer[6]) >> 3;
    const uint16_t y = (sensor->rxBuffer[7] << 8 | sensor->rxBuffer[8]) >> 3;
    int16_t result[3];

    sensor->pressed = true;

    result[0] = (int16_t)(((x - sensor->xMin) * sensor->xRes)
          / (sensor->xMax - sensor->xMin));
    result[1] = (int16_t)(((y - sensor->yMin) * sensor->yRes)
          / (sensor->yMax - sensor->yMin));
    result[2] = (int16_t)z;

    sensor->onResultCallback(sensor->callbackArgument, &result, sizeof(result));
  }
  else
  {
    /* Touch panel released */
    sensor->pressed = false;
  }
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct XPT2046 * const sensor = object;

  pinSet(sensor->cs);
  ifSetParam(sensor->bus, IF_RELEASE, 0);

  sensor->state = STATE_PROCESS;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void onPinEvent(void *object)
{
  struct XPT2046 * const sensor = object;

  sensor->state = STATE_READ;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct XPT2046 * const sensor = object;

  sensor->state = STATE_READ;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void startReading(struct XPT2046 *sensor)
{
  static const uint8_t txBuffer[11] = {
      CTRL_Z1_POS | CTRL_ADC_ON,
      0x00,
      CTRL_Z2_POS | CTRL_ADC_ON,
      0x00,
      CTRL_HI_X | CTRL_ADC_ON,
      0x00,
      CTRL_HI_Y | CTRL_ADC_ON,
      0x00,
      CTRL_HI_Y | CTRL_SER,
      0x00,
      0x00
  };
  static_assert(sizeof(sensor->rxBuffer) == sizeof(txBuffer),
      "Incorrect buffer configuration");

  /* Lock the interface */
  ifSetParam(sensor->bus, IF_ACQUIRE, 0);

  ifSetParam(sensor->bus, IF_SPI_BIDIRECTIONAL, 0);
  ifSetParam(sensor->bus, IF_ZEROCOPY, 0);
  ifSetCallback(sensor->bus, onBusEvent, sensor);

  if (sensor->rate != 0)
    ifSetParam(sensor->bus, IF_RATE, &sensor->rate);

  pinReset(sensor->cs);

  ifRead(sensor->bus, sensor->rxBuffer, sizeof(sensor->rxBuffer));
  ifWrite(sensor->bus, txBuffer, sizeof(txBuffer));
}
/*----------------------------------------------------------------------------*/
static enum Result tsInit(void *object, const void *configBase)
{
  const struct XPT2046Config * const config = configBase;
  assert(config);
  assert(config->bus && config->event && config->timer);
  assert(config->x && config->y);

  struct XPT2046 * const sensor = object;

  sensor->cs = pinInit(config->cs);
  if (!pinValid(sensor->cs))
    return E_VALUE;
  pinOutput(sensor->cs, true);

  sensor->bus = config->bus;
  sensor->event = config->event;
  sensor->timer = config->timer;
  sensor->rate = config->rate;

  sensor->callbackArgument = 0;
  sensor->onErrorCallback = 0;
  sensor->onResultCallback = 0;
  sensor->onUpdateCallback = 0;

  sensor->state = STATE_IDLE;
  sensor->pressed = false;
  sensor->start = false;
  sensor->stop = false;

  sensor->threshold = config->threshold;
  sensor->xRes = config->x;
  sensor->xMax = sensor->xRes;
  sensor->xMin = 0;
  sensor->yRes = config->y;
  sensor->yMax = sensor->yRes;
  sensor->yMin = 0;

  const uint32_t overflow =
      (timerGetFrequency(sensor->timer) + (UPDATE_FREQ - 1)) / UPDATE_FREQ;

  interruptSetCallback(sensor->event, onPinEvent, sensor);
  timerSetAutostop(sensor->timer, true);
  timerSetCallback(sensor->timer, onTimerEvent, sensor);
  timerSetOverflow(sensor->timer, overflow);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void tsDeinit(void *object)
{
  struct XPT2046 * const sensor = object;

  timerDisable(sensor->timer);
  timerSetCallback(sensor->timer, 0, 0);

  interruptDisable(sensor->event);
  interruptSetCallback(sensor->event, 0, 0);
}
/*----------------------------------------------------------------------------*/
static const char *tsGetFormat(const void *object __attribute__((unused)))
{
  return "i16i16i16";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus tsGetStatus(const void *object)
{
  const struct XPT2046 * const sensor = object;
  return sensor->state == STATE_IDLE ? SENSOR_IDLE : SENSOR_BUSY;
}
/*----------------------------------------------------------------------------*/
static void tsSetCallbackArgument(void *object, void *argument)
{
  struct XPT2046 * const sensor = object;
  sensor->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
static void tsSetErrorCallback(void *object,
    void (*callback)(void *, enum SensorResult))
{
  struct XPT2046 * const sensor = object;
  sensor->onErrorCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void tsSetResultCallback(void *object,
    void (*callback)(void *, const void *, size_t))
{
  struct XPT2046 * const sensor = object;
  sensor->onResultCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void tsSetUpdateCallback(void *object, void (*callback)(void *))
{
  struct XPT2046 * const sensor = object;
  sensor->onUpdateCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void tsReset(void *object __attribute__((unused)))
{
}
/*----------------------------------------------------------------------------*/
static void tsSample(void *object)
{
  struct XPT2046 * const sensor = object;

  assert(sensor->onResultCallback);
  assert(sensor->onUpdateCallback);

  sensor->start = true;
  sensor->stop = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void tsStart(void *object)
{
  struct XPT2046 * const sensor = object;

  assert(sensor->onResultCallback);
  assert(sensor->onUpdateCallback);

  sensor->start = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void tsStop(void *object)
{
  struct XPT2046 * const sensor = object;

  sensor->stop = true;
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static bool tsUpdate(void *object)
{
  struct XPT2046 * const sensor = object;
  bool busy;
  bool updated;

  do
  {
    busy = false;
    updated = false;

    switch (sensor->state)
    {
      case STATE_IDLE:
        if (sensor->start)
        {
          sensor->start = false;

          if (!sensor->stop)
          {
            sensor->state = STATE_EVENT_WAIT;
            interruptEnable(sensor->event);
          }
          else
          {
            sensor->state = STATE_READ;
            updated = true;
          }
        }
        break;

      case STATE_EVENT_WAIT:
        if (sensor->stop)
        {
          interruptDisable(sensor->event);
          timerDisable(sensor->timer);

          sensor->state = STATE_IDLE;
          sensor->stop = false;
        }
        break;

      case STATE_READ:
        interruptDisable(sensor->event);

        sensor->state = STATE_READ_WAIT;
        startReading(sensor);

        busy = true;
        break;

      case STATE_READ_WAIT:
        busy = true;
        break;

      case STATE_PROCESS:
        calcPosition(sensor);

        if (!sensor->stop)
        {
          sensor->state = STATE_EVENT_WAIT;

          if (sensor->pressed)
            timerEnable(sensor->timer);
          else
            interruptEnable(sensor->event);
        }
        else
        {
          sensor->pressed = false;
          sensor->stop = false;
          sensor->state = STATE_IDLE;
        }
        break;
    }
  }
  while (updated);

  return busy;
}
/*----------------------------------------------------------------------------*/
void xpt2046ResetCalibration(struct XPT2046 *sensor)
{
  sensor->xMax = sensor->xRes;
  sensor->xMin = 0;
  sensor->yMax = sensor->yRes;
  sensor->yMin = 0;
}
/*----------------------------------------------------------------------------*/
void xpt2046SetCalibration(struct XPT2046 *sensor,
    uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by)
{
  sensor->xMax = bx;
  sensor->xMin = ax;
  sensor->yMax = by;
  sensor->yMin = ay;
}
/*----------------------------------------------------------------------------*/
void xpt2046SetSensitivity(struct XPT2046 *sensor, uint16_t threshold)
{
  sensor->threshold = threshold;
}