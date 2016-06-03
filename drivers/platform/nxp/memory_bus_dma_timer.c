/*
 * memory_bus_dma_timer.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <halm/platform/nxp/gptimer_defs.h>
#include <dpm/drivers/platform/nxp/memory_bus_dma_timer.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static enum result setupChannels(struct MemoryBusDmaTimer *,
    const struct MemoryBusDmaTimerConfig *);
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
    .size = sizeof(struct MemoryBusDmaTimer),
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
const struct TimerClass * const MemoryBusDmaTimer = &timerTable;
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  reg->IR = reg->IR | timer->interruptValue;
  /* Due to a strange behavior DMA requests should be cleared twice */
  reg->IR = timer->interruptValue;

  if (timer->callback)
    timer->callback(timer->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static enum result setupChannels(struct MemoryBusDmaTimer *timer,
    const struct MemoryBusDmaTimerConfig *config)
{
  uint8_t mask = 0;

  const int leadingChannel = gpTimerConfigMatchPin(config->channel,
      config->leading);
  if (leadingChannel == -1)
    return E_VALUE;
  timer->leadingChannel = leadingChannel;
  mask |= 1 << timer->leadingChannel;

  const int trailingChannel = gpTimerConfigMatchPin(config->channel,
      config->trailing);
  if (trailingChannel == -1)
    return E_VALUE;
  timer->trailingChannel = trailingChannel;
  mask |= 1 << timer->trailingChannel;

  /* Only 3 channels are used, allocation always succeeds */
  timer->resetChannel = gpTimerAllocateChannel(mask);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum result tmrInit(void *object, const void *configPtr)
{
  const struct MemoryBusDmaTimerConfig * const config = configPtr;
  const struct GpTimerBaseConfig parentConfig = {
      .channel = config->channel
  };
  struct MemoryBusDmaTimer * const timer = object;
  uint32_t captureControlValue;
  enum result res;

  if (config->input)
  {
    const uint8_t captureChannel = gpTimerConfigCapturePin(config->channel,
        config->input, PIN_PULLDOWN);

    /* Clock polarity depends on control signal type */
    captureControlValue = CTCR_INPUT(captureChannel) | MODE_FALLING;
  }
  else
    captureControlValue = 0;

  /* Call base class constructor */
  if ((res = GpTimerBase->init(object, &parentConfig)) != E_OK)
    return res;

  /* Configure timer channels */
  if ((res = setupChannels(timer, config)) != E_OK)
    return res;

  timer->parent.handler = interruptHandler;
  timer->callback = 0;
  timer->control = config->control;
  timer->leading = config->inversion;

  LPC_TIMER_Type * const reg = timer->parent.reg;

  reg->TCR = TCR_CRES; /* Timer is disabled by default */
  reg->CCR = 0;
  reg->CTCR = captureControlValue;
  reg->PR = 0;

  /* Calculate values for fast timer setup */
  timer->interruptValue = IR_MATCH_INTERRUPT(timer->leadingChannel)
      | IR_MATCH_INTERRUPT(timer->trailingChannel);
  timer->matchValue = MCR_RESET(timer->resetChannel);

  reg->IR = timer->interruptValue; /* Clear pending interrupt requests */
  reg->MCR = 0;

  /* Configure match outputs */
  reg->EMR = EMR_CONTROL(timer->leadingChannel, CONTROL_TOGGLE)
      | EMR_CONTROL(timer->trailingChannel, CONTROL_TOGGLE);

  if (config->inversion)
    reg->EMR |= EMR_EXTERNAL_MATCH(timer->leadingChannel);

  if (config->cycle)
    tmrSetOverflow(timer, config->cycle); /* Call class function directly */

  irqSetPriority(timer->parent.irq, config->priority);
  irqEnable(timer->parent.irq);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void tmrDeinit(void *object)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  irqDisable(timer->parent.irq);
  reg->TCR = 0;
  GpTimerBase->deinit(timer);
}
/*----------------------------------------------------------------------------*/
static void tmrCallback(void *object, void (*callback)(void *), void *argument)
{
  struct MemoryBusDmaTimer * const timer = object;

  timer->callbackArgument = argument;
  timer->callback = callback;
}
/*----------------------------------------------------------------------------*/
static void tmrSetEnabled(void *object, bool state)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  reg->TCR = TCR_CRES;
  reg->IR = timer->interruptValue; /* Clear pending interrupt requests */

  if (state)
  {
    reg->MCR = timer->matchValue;
    reg->TCR = TCR_CEN;
  }
}
/*----------------------------------------------------------------------------*/
static enum result tmrSetFrequency(void *object __attribute__((unused)),
    uint32_t frequency __attribute__((unused)))
{
  return E_ERROR;
}
/*----------------------------------------------------------------------------*/
static enum result tmrSetOverflow(void *object, uint32_t overflow)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->parent.reg;

  if (timer->control)
  {
    reg->MR[timer->leadingChannel] = 1;
    reg->MR[timer->trailingChannel] = overflow - 1;
    reg->MR[timer->resetChannel] = overflow;
  }
  else
  {
    reg->MR[timer->leadingChannel] = (overflow >> 2) - 1;
    reg->MR[timer->trailingChannel] = (overflow >> 2) + (overflow >> 1) - 1;
    reg->MR[timer->resetChannel] = overflow - 1;
  }

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
  const struct MemoryBusDmaTimer * const timer = object;
  const LPC_TIMER_Type * const reg = timer->parent.reg;

  return reg->TC;
}
/*----------------------------------------------------------------------------*/
uint8_t memoryBusDmaTimerPrimaryChannel(const struct MemoryBusDmaTimer *timer)
{
  return timer->leading ? timer->leadingChannel : timer->trailingChannel;
}
