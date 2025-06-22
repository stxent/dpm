/*
 * tea57xx.c
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/radio/tea57xx.h>
#include <dpm/radio/tea57xx_defs.h>
#include <halm/generic/i2c.h>
#include <halm/timer.h>
#include <halm/wq.h>
#include <xcore/atomic.h>
#include <assert.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
enum
{
  STATE_IDLE,

  STATE_READ_DATA,
  STATE_READ_DATA_WAIT,
  STATE_WRITE_DATA,
  STATE_WRITE_DATA_WAIT,

  STATE_ERROR_WAIT,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
};
/*----------------------------------------------------------------------------*/
static void busInit(struct TEA57XX *, bool);
static void invokeUpdate(struct TEA57XX *);
static void onBusEvent(void *);
static void onTimerEvent(void *);
static void startBusTimeout(struct Timer *);
static void updateTask(void *);
/*----------------------------------------------------------------------------*/
static enum Result teaInit(void *, const void *);
static void teaDeinit(void *);
/*----------------------------------------------------------------------------*/
const struct EntityClass * const TEA57XX = &(const struct EntityClass){
    .size = sizeof(struct TEA57XX),
    .init = teaInit,
    .deinit = teaDeinit
};
/*----------------------------------------------------------------------------*/
static void busInit(struct TEA57XX *radio, bool read)
{
  /* Lock the interface */
  ifSetParam(radio->bus, IF_ACQUIRE, NULL);

  ifSetParam(radio->bus, IF_ADDRESS, &radio->address);
  ifSetParam(radio->bus, IF_ZEROCOPY, NULL);
  ifSetCallback(radio->bus, onBusEvent, radio);

  if (radio->rate)
    ifSetParam(radio->bus, IF_RATE, &radio->rate);

  if (read)
    ifSetParam(radio->bus, IF_I2C_REPEATED_START, NULL);

  /* Start bus watchdog */
  startBusTimeout(radio->timer);
}
/*----------------------------------------------------------------------------*/
static void invokeUpdate(struct TEA57XX *radio)
{
  assert(radio->updateCallback != NULL || radio->wq != NULL);

  if (radio->updateCallback != NULL)
  {
    radio->updateCallback(radio->updateCallbackArgument);
  }
  else if (!radio->pending)
  {
    radio->pending = true;

    if (wqAdd(radio->wq, updateTask, radio) != E_OK)
      radio->pending = false;
  }
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct TEA57XX * const radio = object;

  timerDisable(radio->timer);
  ifSetCallback(radio->bus, NULL, NULL);
  ifSetParam(radio->bus, IF_RELEASE, NULL);

  invokeUpdate(radio);
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct TEA57XX * const radio = object;

  switch (radio->state)
  {
    case STATE_ERROR_WAIT:
      radio->state = STATE_ERROR_INTERFACE;
      break;

    default:
      ifSetCallback(radio->bus, NULL, NULL);
      ifSetParam(radio->bus, IF_RELEASE, NULL);
      radio->state = STATE_ERROR_TIMEOUT;
      break;
  }

  invokeUpdate(radio);
}
/*----------------------------------------------------------------------------*/
static void startBusTimeout(struct Timer *timer)
{
  timerSetOverflow(timer, timerGetFrequency(timer) / 10);
  timerSetValue(timer, 0);
  timerEnable(timer);
}
/*----------------------------------------------------------------------------*/
static void updateTask(void *argument)
{
  struct TEA57XX * const radio = argument;

  radio->pending = false;
  tea57xxUpdate(radio);
}
/*----------------------------------------------------------------------------*/
static enum Result teaInit(void *object, const void *configBase)
{
  const struct TEA57XXConfig * const config = configBase;
  assert(config != NULL);
  assert(config->bus != NULL && config->timer != NULL);

  struct TEA57XX * const radio = object;

  radio->callback = NULL;
  radio->errorCallback = NULL;
  radio->idleCallback = NULL;
  radio->updateCallback = NULL;

  radio->bus = config->bus;
  radio->timer = config->timer;
  radio->wq = NULL;
  radio->flags = false;
  radio->level = 0;
  radio->state = STATE_IDLE;
  radio->blocking = true;
  radio->pending = false;

  radio->address = config->address;
  radio->rate = config->rate;

  /* Initial configuration */
  radio->config[0] = 0;
  radio->config[1] = 0;
  radio->config[2] = WDB3_SUD | WDB3_SSL(1) | WDB3_HLSI;
  radio->config[3] = WDB4_XTAL;
  radio->config[4] = 0;

  timerSetAutostop(radio->timer, true);
  timerSetCallback(radio->timer, onTimerEvent, radio);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void teaDeinit(void *object)
{
  struct TEA57XX * const radio = object;

  timerDisable(radio->timer);
  timerSetCallback(radio->timer, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
void tea57xxSetErrorCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct TEA57XX * const radio = object;

  assert(callback != NULL);

  radio->errorCallbackArgument = argument;
  radio->errorCallback = callback;
}
/*----------------------------------------------------------------------------*/
void tea57xxSetIdleCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct TEA57XX * const radio = object;

  assert(callback != NULL);

  radio->idleCallbackArgument = argument;
  radio->idleCallback = callback;
}
/*----------------------------------------------------------------------------*/
void tea57xxSetUpdateCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct TEA57XX * const radio = object;

  assert(callback != NULL);
  assert(radio->wq == NULL);

  radio->updateCallbackArgument = argument;
  radio->updateCallback = callback;
}
/*----------------------------------------------------------------------------*/
void tea57xxSetUpdateWorkQueue(void *object, struct WorkQueue *wq)
{
  struct TEA57XX * const radio = object;

  assert(wq != NULL);
  assert(radio->updateCallback == NULL);

  radio->wq = wq;
}
/*----------------------------------------------------------------------------*/
bool tea57xxUpdate(void *object)
{
  struct TEA57XX * const radio = object;
  bool busy;
  bool updated;

  do
  {
    busy = false;
    updated = false;

    switch (radio->state)
    {
      case STATE_IDLE:
      {
        const uint8_t flags = atomicLoad(&radio->flags);

        if (flags & FLAG_RESET)
        {
          radio->level = 0;
          radio->state = STATE_WRITE_DATA;
          updated = true;
        }
        else if (flags & FLAG_STATUS)
        {
          radio->state = STATE_READ_DATA;
          updated = true;
        }
        else if (flags & FLAG_CONFIG)
        {
          radio->state = STATE_WRITE_DATA;
          updated = true;
        }
        break;
      }

      case STATE_READ_DATA:
        radio->state = STATE_READ_DATA_WAIT;
        atomicFetchAnd(&radio->flags, ~FLAG_STATUS);

        busInit(radio, true);
        ifRead(radio->bus, radio->buffer, sizeof(radio->buffer));
        busy = true;
        break;

      case STATE_READ_DATA_WAIT:
        radio->state = STATE_IDLE;
        radio->level = RDB4_LEV_VALUE(radio->buffer[3]);

        /* Idle callback for Bus Handlers */
        if (radio->idleCallback != NULL)
          radio->idleCallback(radio->idleCallbackArgument);

        updated = true;
        break;

      case STATE_WRITE_DATA:
        radio->state = STATE_WRITE_DATA_WAIT;
        atomicFetchAnd(&radio->flags, ~(FLAG_RESET | FLAG_CONFIG));

        memcpy(radio->buffer, radio->config, sizeof(radio->buffer));

        busInit(radio, false);
        ifWrite(radio->bus, radio->buffer, sizeof(radio->buffer));
        busy = true;
        break;

      case STATE_WRITE_DATA_WAIT:
        radio->state = STATE_IDLE;

        /* Idle callback for Bus Handlers */
        if (radio->idleCallback != NULL)
          radio->idleCallback(radio->idleCallbackArgument);

        updated = true;
        break;

      case STATE_ERROR_INTERFACE:
      case STATE_ERROR_TIMEOUT:
        radio->state = STATE_IDLE;

        /* Error callback for Bus Handlers */
        if (radio->errorCallback != NULL)
          radio->errorCallback(radio->errorCallbackArgument);
        /* User callback for Interface class */
        if (radio->callback != NULL)
          radio->callback(radio->callbackArgument);

        updated = true;
        break;

      default:
        break;
    }
  }
  while (updated);

  return busy;
}
/*----------------------------------------------------------------------------*/
uint8_t tea57xxGetLevel(const struct TEA57XX *radio)
{
  return radio->level;
}
/*----------------------------------------------------------------------------*/
void tea57xxRequestLevel(struct TEA57XX *radio)
{
  atomicFetchOr(&radio->flags, FLAG_STATUS);
  invokeUpdate(radio);
}
/*----------------------------------------------------------------------------*/
void tea57xxSetFrequency(struct TEA57XX *radio, uint32_t frequency)
{
  const uint32_t multiplier = 4 * (frequency + 225000) / 32768;

  radio->config[0] &= ~WDB1_PLL_MASK;
  radio->config[0] |= WDB1_PLL((uint8_t)(multiplier >> 8));
  radio->config[1] &= ~WDB2_PLL_MASK;
  radio->config[1] |= WDB2_PLL((uint8_t)multiplier);

  atomicFetchOr(&radio->flags, FLAG_CONFIG);
  invokeUpdate(radio);
}
/*----------------------------------------------------------------------------*/
void tea57xxSetMute(struct TEA57XX *radio, enum CodecChannel channels)
{
  radio->config[2] &= ~(WDB3_ML | WDB3_MR);
  if (channels & CHANNEL_LEFT)
    radio->config[2] |= WDB3_ML;
  if (channels & CHANNEL_RIGHT)
    radio->config[2] |= WDB3_MR;

  atomicFetchOr(&radio->flags, FLAG_CONFIG);
  invokeUpdate(radio);
}
