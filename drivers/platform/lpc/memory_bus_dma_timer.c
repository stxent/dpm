/*
 * memory_bus_dma_timer.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/drivers/platform/lpc/memory_bus_dma_timer.h>
#include <halm/platform/lpc/gptimer_defs.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static inline uint32_t getMaxValue(const struct MemoryBusDmaTimer *);
static void interruptHandler(void *);
static void setupChannels(struct MemoryBusDmaTimer *, uint8_t,
    PinNumber, PinNumber, PinNumber);
/*----------------------------------------------------------------------------*/
static enum Result tmrClockInit(void *, const void *);
static enum Result tmrControlInit(void *, const void *);
static void tmrDeinit(void *);
static void tmrCallback(void *, void (*)(void *), void *);
static void tmrEnable(void *);
static void tmrDisable(void *);
static void tmrClockSetOverflow(void *, uint32_t);
static void tmrControlSetOverflow(void *, uint32_t);
/*----------------------------------------------------------------------------*/
const struct TimerClass * const MemoryBusDmaClock =
    &(const struct TimerClass){
    .size = sizeof(struct MemoryBusDmaTimer),
    .init = tmrClockInit,
    .deinit = tmrDeinit,

    .enable = tmrEnable,
    .disable = tmrDisable,
    .setAutostop = 0,
    .setCallback = tmrCallback,
    .getFrequency = 0,
    .setFrequency = 0,
    .getOverflow = 0,
    .setOverflow = tmrClockSetOverflow,
    .getValue = 0,
    .setValue = 0
};

const struct TimerClass * const MemoryBusDmaControl =
    &(const struct TimerClass){
    .size = sizeof(struct MemoryBusDmaTimer),
    .init = tmrControlInit,
    .deinit = tmrDeinit,

    .enable = tmrEnable,
    .disable = tmrDisable,
    .setCallback = 0,
    .getFrequency = 0,
    .setFrequency = 0,
    .getOverflow = 0,
    .setOverflow = tmrControlSetOverflow,
    .getValue = 0,
    .setValue = 0
};
/*----------------------------------------------------------------------------*/
static inline uint32_t getMaxValue(const struct MemoryBusDmaTimer *timer)
{
  return MASK(timer->base.resolution);
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->IR = IR_MATCH_MASK;

  /*
   * DMA requests in Interrupt Register must be cleared twice
   * even if the IR contains zeros.
   */
  reg->IR = IR_MATCH_MASK;

  if (timer->callback)
    timer->callback(timer->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void setupChannels(struct MemoryBusDmaTimer *timer,
    uint8_t channel, PinNumber leading, PinNumber trailing, PinNumber select)
{
  uint8_t mask = 0;

  timer->leading = gpTimerConfigMatchPin(channel, leading);
  mask |= 1 << timer->leading;

  timer->trailing = gpTimerConfigMatchPin(channel, trailing);
  mask |= 1 << timer->trailing;

  if (select)
  {
    timer->select = gpTimerConfigMatchPin(channel, select);
    mask |= 1 << timer->trailing;
  }
  else
    timer->select = GPTIMER_EVENT_END;

  timer->reset = gpTimerAllocateChannel(mask);
}
/*----------------------------------------------------------------------------*/
static enum Result tmrClockInit(void *object, const void *configPtr)
{
  const struct MemoryBusDmaClockConfig * const config = configPtr;
  const struct GpTimerBaseConfig parentConfig = {
      .channel = config->channel
  };
  struct MemoryBusDmaTimer * const timer = object;
  enum Result res;

  /* Call base class constructor */
  if ((res = GpTimerBase->init(object, &parentConfig)) != E_OK)
    return res;

  /* Configure timer channels */
  setupChannels(timer, config->channel, config->leading, config->trailing, 0);

  timer->base.handler = interruptHandler;
  timer->callback = 0;
  timer->match = MCR_RESET(timer->reset);

  LPC_TIMER_Type * const reg = timer->base.reg;

  /* Timer is disabled by default */
  reg->TCR = TCR_CRES;
  reg->CCR = 0;
  reg->CTCR = 0;
  reg->PR = 0;

  /* Clear all pending interrupts */
  reg->IR = IR_MATCH_MASK | IR_CAPTURE_MASK;
  reg->MCR = 0;

  /* Configure match channels */
  reg->EMR = EMR_CONTROL(timer->leading, CONTROL_TOGGLE)
      | EMR_CONTROL(timer->trailing, CONTROL_TOGGLE);

  if (config->inversion)
    reg->EMR |= EMR_EXTERNAL_MATCH(timer->leading);

  tmrClockSetOverflow(timer, config->cycle);
  irqSetPriority(timer->base.irq, config->priority);
  irqEnable(timer->base.irq);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum Result tmrControlInit(void *object, const void *configPtr)
{
  const struct MemoryBusDmaControlConfig * const config = configPtr;
  const struct GpTimerBaseConfig parentConfig = {
      .channel = config->channel
  };
  struct MemoryBusDmaTimer * const timer = object;
  enum Result res;

  const uint8_t captureChannel = gpTimerConfigCapturePin(config->channel,
      config->input, PIN_PULLDOWN);
  const uint32_t captureControl = CTCR_INPUT(captureChannel) | MODE_TOGGLE;

  /* Call base class constructor */
  if ((res = GpTimerBase->init(object, &parentConfig)) != E_OK)
    return res;

  /* Configure timer channels */
  setupChannels(timer, config->channel, config->leading, config->trailing,
      config->select);

  timer->callback = 0;
  timer->match = 0;

  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->TCR = TCR_CRES; /* Timer is disabled by default */
  reg->CCR = 0;
  reg->CTCR = captureControl;
  reg->PR = 0;

  /* Clear all pending interrupts */
  reg->IR = IR_MATCH_MASK | IR_CAPTURE_MASK;
  reg->MCR = 0;

  /* Configure match channels */
  reg->EMR = EMR_CONTROL(timer->leading, CONTROL_TOGGLE)
      | EMR_CONTROL(timer->trailing, CONTROL_TOGGLE);
  reg->MR[timer->reset] = getMaxValue(timer);

  if (config->inversion)
    reg->EMR |= EMR_EXTERNAL_MATCH(timer->leading);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void tmrDeinit(void *object)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  irqDisable(timer->base.irq);
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
static void tmrEnable(void *object)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->TCR = TCR_CRES;
  reg->IR = IR_MATCH_MASK;
  reg->MCR = timer->match;
  reg->TCR = TCR_CEN;
}
/*----------------------------------------------------------------------------*/
static void tmrDisable(void *object)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->TCR = TCR_CRES;
  reg->IR = IR_MATCH_MASK;
}
/*----------------------------------------------------------------------------*/
static void tmrClockSetOverflow(void *object, uint32_t overflow)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->MR[timer->leading] = (overflow >> 2) - 1;
  reg->MR[timer->trailing] = (overflow >> 2) + (overflow >> 1) - 1;
  reg->MR[timer->reset] = overflow - 1;
}
/*----------------------------------------------------------------------------*/
static void tmrControlSetOverflow(void *object, uint32_t overflow)
{
  struct MemoryBusDmaTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->MR[timer->leading] = 2;
  reg->MR[timer->trailing] = (overflow << 1) - 1;

  if (timer->select != GPTIMER_EVENT_END)
    reg->MR[timer->select] = overflow << 1;
}
