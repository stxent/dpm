/*
 * button_complex.c
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#include <halm/interrupt.h>
#include <halm/timer.h>
#include <dpm/button_complex.h>
#include <assert.h>
#include <limits.h>
/*----------------------------------------------------------------------------*/
#define DEBOUNCE_FREQUENCY 100
/*----------------------------------------------------------------------------*/
static void onPinInterrupt(void *);
static void onTimerOverflow(void *);

static enum Result buttonInit(void *, const void *);
static void buttonDeinit(void *);
/*----------------------------------------------------------------------------*/
const struct EntityClass * const ButtonComplex = &(const struct EntityClass){
    .size = sizeof(struct ButtonComplex),
    .init = buttonInit,
    .deinit = buttonDeinit
};
/*----------------------------------------------------------------------------*/
static void onPinInterrupt(void *argument)
{
  struct ButtonComplex * const button = argument;

  interruptDisable(button->interrupt);
  timerEnable(button->timer);
}
/*----------------------------------------------------------------------------*/
static void onTimerOverflow(void *argument)
{
  struct ButtonComplex * const button = argument;
  void (*callback)(void *) = NULL;
  void *callbackArgument = NULL;
  bool stop = false;

  if (pinRead(button->pin) == button->level)
  {
    if (button->counter == button->delayWait)
    {
      if (!button->delayHold)
        stop = true;

      callback = button->pressCallback;
      callbackArgument = button->pressCallbackArgument;
    }

    if (button->delayHold && button->counter == button->delayHold)
    {
      button->counter = button->delayWait;

      stop = true;
      callback = button->longPressCallback;
      callbackArgument = button->longPressCallbackArgument;
    }

    if (!stop && button->counter < USHRT_MAX)
      ++button->counter;
  }
  else
  {
    if (button->counter == 0)
    {
      stop = true;
      callback = button->releaseCallback;
      callbackArgument = button->releaseCallbackArgument;
    }

    if (!stop && button->counter > 0)
      --button->counter;
  }

  if (stop)
  {
    timerDisable(button->timer);
    interruptEnable(button->interrupt);
  }

  if (callback != NULL)
    callback(callbackArgument);
}
/*----------------------------------------------------------------------------*/
static enum Result buttonInit(void *object, const void *configBase)
{
  const struct ButtonComplexConfig * const config = configBase;
  assert(config != NULL);
  assert(config->interrupt != NULL && config->timer != NULL);
  assert(!config->hold || config->hold > config->delay);

  struct ButtonComplex * const button = object;

  button->pin = pinInit(config->pin);
  assert(pinValid(button->pin));

  button->longPressCallback = NULL;
  button->longPressCallbackArgument = NULL;
  button->pressCallback = NULL;
  button->pressCallbackArgument = NULL;
  button->releaseCallback = NULL;
  button->releaseCallbackArgument = NULL;

  button->interrupt = config->interrupt;
  button->timer = config->timer;
  button->counter = 0;
  button->delayHold = config->hold;
  button->delayWait = config->delay;
  button->level = config->level;

  const uint32_t overflow =
      (timerGetFrequency(button->timer) + DEBOUNCE_FREQUENCY - 1)
          / DEBOUNCE_FREQUENCY;

  interruptSetCallback(button->interrupt, onPinInterrupt, button);
  timerSetCallback(button->timer, onTimerOverflow, button);
  timerSetOverflow(button->timer, overflow);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void buttonDeinit(void *object)
{
  struct ButtonComplex * const button = object;

  buttonComplexDisable(button);

  timerSetCallback(button->timer, NULL, NULL);
  interruptSetCallback(button->interrupt, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
void buttonComplexEnable(struct ButtonComplex *button)
{
  interruptEnable(button->interrupt);
}
/*----------------------------------------------------------------------------*/
void buttonComplexDisable(struct ButtonComplex *button)
{
  timerDisable(button->timer);
  interruptDisable(button->interrupt);
}
/*----------------------------------------------------------------------------*/
void buttonComplexSetLongPressCallback(struct ButtonComplex *button,
    void (*callback)(void *), void *argument)
{
  button->longPressCallback = callback;
  button->longPressCallbackArgument = argument;
}
    /*----------------------------------------------------------------------------*/
void buttonComplexSetPressCallback(struct ButtonComplex *button,
    void (*callback)(void *), void *argument)
{
  button->pressCallback = callback;
  button->pressCallbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
void buttonComplexSetReleaseCallback(struct ButtonComplex *button,
    void (*callback)(void *), void *argument)
{
  button->releaseCallback = callback;
  button->releaseCallbackArgument = argument;
}
