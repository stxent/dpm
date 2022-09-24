/*
 * irda_timer.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/irda_timer.h>
#include <halm/platform/lpc/gptimer_defs.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static void setupChannels(struct IrdaTimer *);
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *, const void *);
static void tmrDeinit(void *);
static void tmrEnable(void *);
static void tmrDisable(void *);
static void tmrSetCallback(void *, void (*)(void *), void *);
static void tmrSetFrequency(void *, uint32_t);
static void tmrSetOverflow(void *, uint32_t);
/*----------------------------------------------------------------------------*/
const struct TimerClass * const IrdaTimer = &(const struct TimerClass){
    .size = sizeof(struct IrdaTimer),
    .init = tmrInit,
    .deinit = tmrDeinit,

    .enable = tmrEnable,
    .disable = tmrDisable,
    .setAutostop = 0,
    .setCallback = tmrSetCallback,
    .getFrequency = 0,
    .setFrequency = tmrSetFrequency,
    .getOverflow = 0,
    .setOverflow = tmrSetOverflow,
    .getValue = 0,
    .setValue = 0
};
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct IrdaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;
  const uint32_t ir = reg->IR;

  reg->IR = ir;

  struct IrdaTimerEvent event = {
      .argument = timer->callbackArgument
  };

  if (ir & IR_MATCH_INTERRUPT(timer->syncChannel))
    event.type = IRDA_TIMER_SYNC;
  else
    event.type = IRDA_TIMER_DATA;

  timer->callback(&event);
}
/*----------------------------------------------------------------------------*/
static void setupChannels(struct IrdaTimer *timer)
{
  /* Allocation always succeeds */
  timer->syncChannel = gpTimerAllocateChannel(0);
  timer->dataChannel = gpTimerAllocateChannel(1 << timer->syncChannel);
}
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *object, const void *configPtr)
{
  const struct IrdaTimerConfig * const config = configPtr;
  const struct GpTimerBaseConfig baseConfig = {
      .channel = config->channel
  };
  struct IrdaTimer * const timer = object;
  enum Result res;

  assert(config->frequency);

  /* Call base class constructor */
  if ((res = GpTimerBase->init(object, &baseConfig)) != E_OK)
    return res;

  timer->base.handler = interruptHandler;
  timer->callback = 0;
  timer->sync = config->sync;

  setupChannels(timer);

  LPC_TIMER_Type * const reg = timer->base.reg;
  uint32_t mcr = MCR_RESET(timer->dataChannel);

  if (!config->master)
    mcr |= MCR_STOP(timer->dataChannel);

  reg->TCR = 0; /* Timer is disabled by default */
  reg->PC = reg->TC = 0;
  reg->CTCR = 0;
  reg->CCR = 0;

  /* Configure interrupts */
  reg->IR = reg->IR; /* Clear pending interrupts */
  reg->MCR = mcr;

  /* Configure timings */
  tmrSetFrequency(timer, config->frequency);
  tmrSetOverflow(timer, config->period);

  irqSetPriority(timer->base.irq, config->priority);
  irqEnable(timer->base.irq);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void tmrDeinit(void *object)
{
  struct IrdaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  irqDisable(timer->base.irq);
  reg->TCR &= ~TCR_CEN;
  GpTimerBase->deinit(timer);
}
/*----------------------------------------------------------------------------*/
static void tmrEnable(void *object)
{
  struct IrdaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->IR = IR_MATCH_MASK;
  reg->PC = reg->TC = 0;
  reg->TCR = TCR_CEN;
}
/*----------------------------------------------------------------------------*/
static void tmrDisable(void *object)
{
  struct IrdaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  /* Stop immediately */
  reg->TCR &= ~TCR_CEN;
}
/*----------------------------------------------------------------------------*/
static void tmrSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct IrdaTimer * const timer = object;
  const uint32_t mask = MCR_INTERRUPT(timer->syncChannel)
      | MCR_INTERRUPT(timer->dataChannel);
  LPC_TIMER_Type * const reg = timer->base.reg;

  timer->callbackArgument = argument;
  timer->callback = callback;

  if (callback)
  {
    reg->IR = IR_MATCH_MASK;
    reg->MCR |= mask;
  }
  else
    reg->MCR &= ~mask;
}
/*----------------------------------------------------------------------------*/
static void tmrSetFrequency(void *object, uint32_t frequency)
{
  struct IrdaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  assert(frequency);

  reg->PR = gpTimerGetClock(object) / frequency - 1;
}
/*----------------------------------------------------------------------------*/
static void tmrSetOverflow(void *object, uint32_t overflow)
{
  struct IrdaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  assert(overflow);

  reg->MR[timer->syncChannel] = overflow - 1 - timer->sync;
  reg->MR[timer->dataChannel] = overflow - 1;
}
