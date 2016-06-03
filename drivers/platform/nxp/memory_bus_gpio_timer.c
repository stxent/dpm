/*
 * memory_bus_gpio_timer.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <halm/platform/nxp/gptimer_defs.h>
#include <dpm/drivers/platform/nxp/memory_bus_gpio_timer.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static enum result setupChannels(struct MemoryBusGpioTimer *,
    const struct MemoryBusGpioTimerConfig *);
/*----------------------------------------------------------------------------*/
static enum result tmrInit(void *, const void *);
static void tmrDeinit(void *);
static void tmrCallback(void *, void (*)(void *), void *);
static void tmrSetEnabled(void *, bool);
static enum result tmrSetFrequency(void *, uint32_t);
static enum result tmrSetOverflow(void *, uint32_t);
static enum result tmrSetValue(void *, uint32_t);
static uint32_t tmrValue(const void *);
/*----------------------------------------------------------------------------*/
static const struct TimerClass timerTable = {
    .size = sizeof(struct MemoryBusGpioTimer),
    .init = tmrInit,
    .deinit = tmrDeinit,

    .callback = tmrCallback,
    .setEnabled = tmrSetEnabled,
    .setFrequency = tmrSetFrequency,
    .setOverflow = tmrSetOverflow,
    .setValue = tmrSetValue,
    .value = tmrValue
};
/*----------------------------------------------------------------------------*/
const struct TimerClass *MemoryBusGpioTimer = &timerTable;
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  if (reg->TCR & TCR_CEN)
    reg->EMR ^= EMR_EXTERNAL_MATCH(timer->eventChannel);

  reg->IR = reg->IR;

  if (timer->callback)
    timer->callback(timer->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static enum result setupChannels(struct MemoryBusGpioTimer *timer,
    const struct MemoryBusGpioTimerConfig *config)
{
  uint8_t mask = 0;

  const int eventChannel = gpTimerConfigMatchPin(config->channel, config->pin);
  if (eventChannel == -1)
    return E_VALUE;
  timer->eventChannel = eventChannel;
  mask |= 1 << timer->eventChannel;

  /* Only 3 channels are used, allocation always succeeds */
  timer->callbackChannel = gpTimerAllocateChannel(mask);
  mask |= 1 << timer->callbackChannel;

  timer->resetChannel = gpTimerAllocateChannel(mask);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum result tmrInit(void *object, const void *configPtr)
{
  const struct MemoryBusGpioTimerConfig * const config = configPtr;
  const struct GpTimerBaseConfig parentConfig = {
      .channel = config->channel
  };
  struct MemoryBusGpioTimer * const timer = object;
  enum result res;

  assert(config->frequency);

  /* Call base class constructor */
  if ((res = GpTimerBase->init(object, &parentConfig)) != E_OK)
    return res;

  timer->parent.handler = interruptHandler;
  timer->inversion = config->inversion;

  if ((res = setupChannels(timer, config)) != E_OK)
    return res;

  LPC_TIMER_Type * const reg = timer->parent.reg;

  reg->TCR = 0; /* Timer is disabled by default */
  reg->PC = reg->TC = 0;
  reg->CTCR = 0;
  reg->CCR = 0;

  /* Configure interrupts */
  reg->IR = reg->IR; /* Clear pending interrupts */
  reg->MCR = MCR_RESET(timer->resetChannel)
      | MCR_INTERRUPT(timer->callbackChannel);

  /* Configure timings */
  reg->PR = gpTimerGetClock(object) / config->frequency - 1;
  reg->EMR = EMR_CONTROL(timer->eventChannel, CONTROL_TOGGLE);
  if (timer->inversion)
    reg->EMR |= EMR_EXTERNAL_MATCH(timer->eventChannel);

  tmrSetOverflow(timer, config->cycle);

  irqSetPriority(timer->parent.irq, config->priority);
  irqEnable(timer->parent.irq);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void tmrDeinit(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  irqDisable(timer->parent.irq);
  reg->TCR &= ~TCR_CEN;
  GpTimerBase->deinit(timer);
}
/*----------------------------------------------------------------------------*/
static void tmrCallback(void *object, void (*callback)(void *), void *argument)
{
  struct MemoryBusGpioTimer * const timer = object;

  timer->callbackArgument = argument;
  timer->callback = callback;
}
/*----------------------------------------------------------------------------*/
static void tmrSetEnabled(void *object, bool state)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  if (state)
  {
    reg->PC = reg->TC = 0;
    reg->MCR &= ~(MCR_INTERRUPT(timer->resetChannel)
        | MCR_STOP(timer->resetChannel));
    reg->TCR = TCR_CEN;
  }
  else
  {
    /* Complete current operation and stop */
    reg->MCR |= MCR_INTERRUPT(timer->resetChannel)
        | MCR_STOP(timer->resetChannel);
  }
}
/*----------------------------------------------------------------------------*/
static enum result tmrSetFrequency(void *object, uint32_t frequency)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  assert(frequency);

  reg->PR = gpTimerGetClock(object) / frequency - 1;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum result tmrSetOverflow(void *object, uint32_t overflow)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  assert(overflow);

  reg->MR[timer->eventChannel] = (overflow >> 2) - 1;
  reg->MR[timer->callbackChannel] = (overflow >> 1) + (overflow >> 2) - 1;
  reg->MR[timer->resetChannel] = overflow - 1;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum result tmrSetValue(void *object __attribute__((unused)),
    uint32_t value __attribute__((unused)))
{
  return E_ERROR;
}
/*----------------------------------------------------------------------------*/
static uint32_t tmrValue(const void *object)
{
  const struct MemoryBusGpioTimer * const timer = object;
  const LPC_TIMER_Type * const reg = timer->parent.reg;

  return reg->TC;
}
