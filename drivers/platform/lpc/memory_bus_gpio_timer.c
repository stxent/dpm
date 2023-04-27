/*
 * memory_bus_gpio_timer.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/memory_bus_gpio_timer.h>
#include <halm/platform/lpc/gptimer_defs.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static void setupChannels(struct MemoryBusGpioTimer *, uint8_t, PinNumber);
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *, const void *);
static void tmrDeinit(void *);
static void tmrSetCallback(void *, void (*)(void *), void *);
static void tmrEnable(void *);
static void tmrDisable(void *);
static void tmrSetFrequency(void *, uint32_t);
static void tmrSetOverflow(void *, uint32_t);
/*----------------------------------------------------------------------------*/
const struct TimerClass * const MemoryBusGpioTimer = &(const struct TimerClass){
    .size = sizeof(struct MemoryBusGpioTimer),
    .init = tmrInit,
    .deinit = tmrDeinit,

    .enable = tmrEnable,
    .disable = tmrDisable,
    .setAutostop = NULL,
    .setCallback = tmrSetCallback,
    .getFrequency = NULL,
    .setFrequency = tmrSetFrequency,
    .getOverflow = NULL,
    .setOverflow = tmrSetOverflow,
    .getValue = NULL,
    .setValue = NULL
};
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  if (reg->TCR & TCR_CEN)
    reg->EMR ^= EMR_EXTERNAL_MATCH(timer->trailing);

  /* Clear all pending interrupts */
  reg->IR = IR_MATCH_MASK;

  if (timer->callback != NULL)
    timer->callback(timer->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void setupChannels(struct MemoryBusGpioTimer *timer, uint8_t channel,
    PinNumber trailing)
{
  uint8_t mask = 0;

  timer->trailing = gpTimerConfigMatchPin(channel, trailing);
  mask |= 1 << timer->trailing;

  /* Only 3 channels are used, allocation always succeeds */
  timer->leading = gpTimerAllocateChannel(mask);
  mask |= 1 << timer->leading;

  timer->reset = gpTimerAllocateChannel(mask);
}
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *object, const void *configPtr)
{
  const struct MemoryBusGpioTimerConfig * const config = configPtr;
  assert(config != NULL);
  assert(config->frequency);

  const struct GpTimerBaseConfig baseConfig = {
      .channel = config->channel
  };
  struct MemoryBusGpioTimer * const timer = object;
  enum Result res;

  /* Call base class constructor */
  if ((res = GpTimerBase->init(object, &baseConfig)) != E_OK)
    return res;

  timer->base.handler = interruptHandler;
  timer->inversion = config->inversion;

  setupChannels(timer, config->channel, config->pin);

  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->TCR = 0; /* Timer is disabled by default */
  reg->PC = reg->TC = 0;
  reg->CTCR = 0;
  reg->CCR = 0;

  /* Clear all pending interrupts */
  reg->IR = IR_MATCH_MASK;

  /* Configure interrupts */
  reg->MCR = MCR_RESET(timer->reset)
      | MCR_INTERRUPT(timer->leading);

  /* Configure timings */
  tmrSetFrequency(timer, config->frequency);
  tmrSetOverflow(timer, config->cycle);

  /* Configure match output */
  reg->EMR = EMR_CONTROL(timer->trailing, CONTROL_TOGGLE);
  if (timer->inversion)
    reg->EMR |= EMR_EXTERNAL_MATCH(timer->trailing);

  irqSetPriority(timer->base.irq, config->priority);
  irqEnable(timer->base.irq);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void tmrDeinit(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  irqDisable(timer->base.irq);
  reg->TCR &= ~TCR_CEN;
  GpTimerBase->deinit(timer);
}
/*----------------------------------------------------------------------------*/
static void tmrSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct MemoryBusGpioTimer * const timer = object;

  timer->callbackArgument = argument;
  timer->callback = callback;
}
/*----------------------------------------------------------------------------*/
static void tmrEnable(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->PC = reg->TC = 0;
  reg->MCR &= ~(MCR_INTERRUPT(timer->reset)
      | MCR_STOP(timer->reset));
  reg->TCR = TCR_CEN;
}
/*----------------------------------------------------------------------------*/
static void tmrDisable(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  /* Complete current operation and stop */
  reg->MCR |= MCR_INTERRUPT(timer->reset)
      | MCR_STOP(timer->reset);
}
/*----------------------------------------------------------------------------*/
static void tmrSetFrequency(void *object, uint32_t frequency)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  assert(frequency);

  reg->PR = gpTimerGetClock(object) / frequency - 1;
}
/*----------------------------------------------------------------------------*/
static void tmrSetOverflow(void *object, uint32_t overflow)
{
  struct MemoryBusGpioTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  assert(overflow);

  reg->MR[timer->trailing] = (overflow >> 2) - 1;
  reg->MR[timer->leading] = (overflow >> 1) + (overflow >> 2) - 1;
  reg->MR[timer->reset] = overflow - 1;
}
