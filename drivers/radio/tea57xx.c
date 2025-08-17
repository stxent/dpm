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
  STATE_SEARCH,
  STATE_SEARCH_WAIT,

  STATE_ERROR_WAIT,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
};
/*----------------------------------------------------------------------------*/
static inline uint32_t frequencyFromMultiplier(const struct TEA57XX *,
    uint16_t);
static inline uint16_t frequencyToMultiplier(const struct TEA57XX *,
    uint32_t);

static void busInit(struct TEA57XX *, bool);
static void invokeUpdate(struct TEA57XX *);
static void loadDefaultConfig(struct TEA57XX *);
static void onBusEvent(void *);
static void onTimerEvent(void *);
static void startBusTimeout(struct Timer *);
static void updateSearchFrequency(struct TEA57XX *);
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
static inline uint32_t frequencyFromMultiplier(const struct TEA57XX *radio,
    uint16_t multiplier)
{
  const int32_t intermediate = (radio->config[2] & WDB3_HLSI) ?
      FREQUENCY_INTERMEDIATE : -FREQUENCY_INTERMEDIATE;
  const int32_t reference = (radio->config[3] & WDB4_XTAL) ?
      (FREQUENCY_XTAL_LS / 4) : (FREQUENCY_XTAL_HS / 4);

  return (uint32_t)((int32_t)multiplier * reference - intermediate);
}
/*----------------------------------------------------------------------------*/
static inline uint16_t frequencyToMultiplier(const struct TEA57XX *radio,
    uint32_t frequency)
{
  const int32_t intermediate = (radio->config[2] & WDB3_HLSI) ?
      FREQUENCY_INTERMEDIATE : -FREQUENCY_INTERMEDIATE;
  const int32_t reference = (radio->config[3] & WDB4_XTAL) ?
      (FREQUENCY_XTAL_LS / 4) : (FREQUENCY_XTAL_HS / 4);

  return ((int32_t)frequency + intermediate) / reference;
}
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
static void loadDefaultConfig(struct TEA57XX *radio)
{
  const uint32_t multiplier = frequencyToMultiplier(radio, FREQUENCY_INITIAL);

  radio->config[0] = WDB1_PLL((uint8_t)(multiplier >> 8));
  radio->config[1] = WDB2_PLL((uint8_t)multiplier);
  radio->config[2] = WDB3_ML | WDB3_MR | WDB3_HLSI
      | WDB3_SSL((uint8_t)radio->sensitivity);
  radio->config[3] = 0;
  radio->config[4] = 0;

  switch (radio->clock)
  {
    case TEA57XX_CLOCK_32K:
      /* 32768 Hz crystal oscillator */
      radio->config[3] |= WDB4_XTAL;
      break;

    case TEA57XX_CLOCK_6M5:
      /* 6.5 MHz external clock */
      radio->config[4] |= WDB5_PLLREF;
      break;

    default:
      /* 13 MHz crystal oscillator */
      break;
  }
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct TEA57XX * const radio = object;

  timerDisable(radio->timer);

  if (ifGetParam(radio->bus, IF_STATUS, NULL) != E_OK)
  {
    radio->state = STATE_ERROR_WAIT;
    startBusTimeout(radio->timer);
  }

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
static void updateSearchFrequency(struct TEA57XX *radio)
{
  uint32_t multiplier = frequencyToMultiplier(radio, radio->frequency);

  if (radio->config[2] & WDB3_SUD)
    multiplier += 4;
  else
    multiplier -= 4;

  radio->config[0] &= ~WDB1_PLL_MASK;
  radio->config[0] |= WDB1_PLL((uint8_t)(multiplier >> 8)) | WDB1_SM;
  radio->config[1] &= ~WDB2_PLL_MASK;
  radio->config[1] |= WDB2_PLL((uint8_t)multiplier);
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
  radio->address = config->address;
  radio->rate = config->rate;

  radio->frequency = FREQUENCY_INITIAL;
  radio->level = 0;
  radio->search = false;

  radio->flags = false;
  radio->state = STATE_IDLE;
  radio->pending = false;

  if (config->clock < TEA57XX_CLOCK_END)
    radio->clock = config->clock;
  else
    return E_VALUE;

  if (config->sensitivity != TEA57XX_SEARCH_DEFAULT)
  {
    if (config->sensitivity >= TEA57XX_SEARCH_END)
      return E_VALUE;
    radio->sensitivity = config->sensitivity;
  }
  else
    radio->sensitivity = TEA57XX_SEARCH_MEDIUM;

  /* Initial configuration */
  loadDefaultConfig(radio);

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
void tea57xxSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct TEA57XX * const radio = object;

  assert(callback != NULL);

  radio->callbackArgument = argument;
  radio->callback = callback;
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
          radio->search = false;

          radio->state = STATE_WRITE_DATA;
          updated = true;
        }
        else if (flags & FLAG_STATUS)
        {
          radio->state = STATE_READ_DATA;
          updated = true;
        }
        else if (flags & FLAG_SEARCH)
        {
          radio->state = STATE_SEARCH;
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
        atomicFetchAnd(&radio->flags, ~FLAG_STATUS);
        radio->state = STATE_READ_DATA_WAIT;

        busInit(radio, true);
        ifRead(radio->bus, radio->buffer, sizeof(radio->buffer));
        busy = true;
        break;

      case STATE_READ_DATA_WAIT:
      {
        const uint16_t multiplier = RDB2_PLL_VALUE(radio->buffer[1])
            | (RDB1_PLL_VALUE(radio->buffer[0]) << 8);

        radio->state = STATE_IDLE;
        radio->frequency = frequencyFromMultiplier(radio, multiplier);
        radio->level = RDB4_LEV_VALUE(radio->buffer[3]);
        radio->search = (radio->config[0] & WDB1_SM) ?
            ((radio->buffer[0] & RDB1_RF) == 0) : 0;

        /* Update configuration */
        radio->config[0] &= ~WDB1_PLL_MASK;
        radio->config[0] |= WDB1_PLL((uint8_t)(multiplier >> 8));
        radio->config[1] &= ~WDB2_PLL_MASK;
        radio->config[1] |= WDB2_PLL((uint8_t)multiplier);
        if (!radio->search)
          radio->config[0] &= ~WDB1_SM;

        /* Idle callback for Bus Handlers */
        if (radio->idleCallback != NULL)
          radio->idleCallback(radio->idleCallbackArgument);
        /* User callback for Interface class */
        if (!radio->flags && radio->callback != NULL)
          radio->callback(radio->callbackArgument);

        updated = true;
        break;
      }

      case STATE_WRITE_DATA:
        atomicFetchAnd(&radio->flags, ~(FLAG_RESET | FLAG_CONFIG));
        radio->state = STATE_WRITE_DATA_WAIT;

        memcpy(radio->buffer, radio->config, sizeof(radio->buffer));
        busInit(radio, false);
        ifWrite(radio->bus, radio->buffer, sizeof(radio->buffer));

        busy = true;
        break;

      case STATE_WRITE_DATA_WAIT:
      {
        const uint16_t multiplier = WDB2_PLL_VALUE(radio->config[1])
            | (WDB1_PLL_VALUE(radio->config[0]) << 8);

        radio->state = STATE_IDLE;
        radio->frequency = frequencyFromMultiplier(radio, multiplier);
        if (radio->config[3] & WDB4_STBY)
        {
          radio->level = 0;
          radio->search = false;
        }

        /* Idle callback for Bus Handlers */
        if (radio->idleCallback != NULL)
          radio->idleCallback(radio->idleCallbackArgument);
        /* User callback for Interface class */
        if (!radio->flags && radio->callback != NULL)
          radio->callback(radio->callbackArgument);

        updated = true;
        break;
      }

      case STATE_SEARCH:
        atomicFetchAnd(&radio->flags, ~FLAG_SEARCH);

        radio->state = STATE_SEARCH_WAIT;
        radio->search = true;
        updateSearchFrequency(radio);

        memcpy(radio->buffer, radio->config, sizeof(radio->buffer));
        busInit(radio, false);
        ifWrite(radio->bus, radio->buffer, sizeof(radio->buffer));

        busy = true;
        break;

      case STATE_SEARCH_WAIT:
        radio->state = STATE_IDLE;

        /* Idle callback for Bus Handlers */
        if (radio->idleCallback != NULL)
          radio->idleCallback(radio->idleCallbackArgument);
        /* User callback for Interface class */
        if (!radio->flags && radio->callback != NULL)
          radio->callback(radio->callbackArgument);

        updated = true;
        break;

      case STATE_ERROR_WAIT:
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
uint32_t tea57xxGetFrequency(const struct TEA57XX *radio)
{
  return radio->frequency;
}
/*----------------------------------------------------------------------------*/
uint32_t tea57xxGetLevel(const struct TEA57XX *radio)
{
  return radio->level;
}
/*----------------------------------------------------------------------------*/
bool tea57xxIsMuted(const struct TEA57XX *radio)
{
  return (radio->config[2] & (WDB3_ML | WDB3_MR)) != 0;
}
/*----------------------------------------------------------------------------*/
bool tea57xxIsSearching(const struct TEA57XX *radio)
{
  return radio->search;
}
/*----------------------------------------------------------------------------*/
void tea57xxRequestState(struct TEA57XX *radio)
{
  atomicFetchOr(&radio->flags, FLAG_STATUS);
  invokeUpdate(radio);
}
/*----------------------------------------------------------------------------*/
void tea57xxReset(struct TEA57XX *radio)
{
  loadDefaultConfig(radio);
  atomicFetchOr(&radio->flags, FLAG_RESET);
  invokeUpdate(radio);
}
/*----------------------------------------------------------------------------*/
void tea57xxSearch(struct TEA57XX *radio, bool up)
{
  if (up)
    radio->config[2] |= WDB3_SUD;
  else
    radio->config[2] &= ~WDB3_SUD;

  atomicFetchOr(&radio->flags, FLAG_STATUS | FLAG_SEARCH);
  invokeUpdate(radio);
}
/*----------------------------------------------------------------------------*/
void tea57xxSetFrequency(struct TEA57XX *radio, uint32_t frequency)
{
  const uint32_t multiplier = frequencyToMultiplier(radio, frequency);

  radio->config[0] &= ~(WDB1_PLL_MASK | WDB1_SM);
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
/*----------------------------------------------------------------------------*/
void tea57xxSuspend(struct TEA57XX *radio)
{
  radio->config[2] |= WDB3_ML | WDB3_MR;
  radio->config[3] |= WDB4_STBY;

  atomicFetchOr(&radio->flags, FLAG_CONFIG);
  invokeUpdate(radio);
}
