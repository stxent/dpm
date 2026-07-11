/*
 * button.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <halm/timer.h>
#include <dpm/button.h>
#include <assert.h>
#include <limits.h>
/*----------------------------------------------------------------------------*/
#define DEBOUNCE_FREQUENCY  100
#define DEBOUNCE_PERIOD     (1000 / DEBOUNCE_FREQUENCY)
/*----------------------------------------------------------------------------*/
static void onPinInterrupt(void *);
static void onTimerOverflow(void *);

static enum Result buttonInit(void *, const void *);
static void buttonDeinit(void *);
static void buttonEnable(void *);
static void buttonDisable(void *);
static void buttonSetCallback(void *, void (*)(void *), void *);
/*----------------------------------------------------------------------------*/
const struct InterruptClass * const Button =
    &(const struct InterruptClass){
    .size = sizeof(struct Button),
    .init = buttonInit,
    .deinit = buttonDeinit,

    .enable = buttonEnable,
    .disable = buttonDisable,
    .setCallback = buttonSetCallback
};
/*----------------------------------------------------------------------------*/
static void onPinInterrupt(void *argument)
{
  struct Button * const button = argument;

  interruptDisable(button->interrupt);
  timerEnable(button->timer);
}
/*----------------------------------------------------------------------------*/
static void onTimerOverflow(void *argument)
{
  struct Button * const button = argument;
  void (*callback)(void *) = NULL;
  void *callbackArgument = NULL;
  bool stop = false;

  if (pinRead(button->pin) == button->level)
  {
    if (button->counter == button->delay)
    {
      stop = true;
      callback = button->callback;
      callbackArgument = button->callbackArgument;
    }

    if (!stop && button->counter < USHRT_MAX)
      ++button->counter;
  }
  else
  {
    if (button->counter == 0)
      stop = true;

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
  const struct ButtonConfig * const config = configBase;
  assert(config != NULL);
  assert(config->interrupt != NULL && config->timer != NULL);

  struct Button * const button = object;

  button->pin = pinInit(config->pin);
  assert(pinValid(button->pin));

  button->callback = NULL;
  button->callbackArgument = NULL;
  button->interrupt = config->interrupt;
  button->timer = config->timer;
  button->counter = 0;
  button->delay = (config->delay + (DEBOUNCE_PERIOD - 1)) / DEBOUNCE_PERIOD;
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
  struct Button * const button = object;

  buttonDisable(button);

  timerSetCallback(button->timer, NULL, NULL);
  interruptSetCallback(button->interrupt, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
static void buttonEnable(void *object)
{
  struct Button * const button = object;
  interruptEnable(button->interrupt);
}
/*----------------------------------------------------------------------------*/
static void buttonDisable(void *object)
{
  struct Button * const button = object;

  timerDisable(button->timer);
  interruptDisable(button->interrupt);
}
/*----------------------------------------------------------------------------*/
static void buttonSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct Button * const button = object;

  button->callbackArgument = argument;
  button->callback = callback;
}
