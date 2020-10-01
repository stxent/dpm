/*
 * button.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <dpm/drivers/button.h>
/*----------------------------------------------------------------------------*/
static void onPinInterrupt(void *);
static void onTimerOverflow(void *);
/*----------------------------------------------------------------------------*/
static enum Result buttonInit(void *, const void *);
static void buttonEnable(void *);
static void buttonDeinit(void *);
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

  button->counter = 0;
  timerEnable(button->timer);
}
/*----------------------------------------------------------------------------*/
static void onTimerOverflow(void *argument)
{
  struct Button * const button = argument;

  if (pinRead(button->pin) == button->level)
  {
    if (button->counter == button->delay)
    {
      timerDisable(button->timer);
      interruptEnable(button->interrupt);

      if (button->callback)
        button->callback(button->callbackArgument);
    }
    else
      ++button->counter;
  }
  else
  {
    if (button->counter == 0)
    {
      timerDisable(button->timer);
      interruptEnable(button->interrupt);
    }
    else
      --button->counter;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result buttonInit(void *object, const void *configBase)
{
  const struct ButtonConfig * const config = configBase;
  assert(config);

  struct Button * const button = object;

  button->pin = pinInit(config->pin);
  assert(pinValid(button->pin));

  button->callback = 0;
  button->interrupt = config->interrupt;
  button->timer = config->timer;
  button->delay = config->delay;
  button->level = config->level;

  interruptSetCallback(button->interrupt, onPinInterrupt, button);
  timerSetCallback(button->timer, onTimerOverflow, button);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void buttonDeinit(void *object)
{
  const struct Button * const button = object;

  timerDisable(button->timer);
  timerSetCallback(button->timer, 0, 0);

  interruptDisable(button->interrupt);
  interruptSetCallback(button->interrupt, 0, 0);
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
