/*
 * sgpio_bus_timer.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/sgpio_bus_timer.h>
#include <halm/platform/lpc/gptimer_defs.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static inline uint32_t getMaxValue(const struct SgpioBusTimer *);
static void setupChannels(struct SgpioBusTimer *, uint8_t);
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *, const void *);
static void tmrDeinit(void *);
static void tmrEnable(void *);
static void tmrDisable(void *);
static void tmrSetOverflow(void *, uint32_t);
/*----------------------------------------------------------------------------*/
const struct TimerClass * const SgpioBusTimer =
    &(const struct TimerClass){
    .size = sizeof(struct SgpioBusTimer),
    .init = tmrInit,
    .deinit = tmrDeinit,

    .enable = tmrEnable,
    .disable = tmrDisable,
    .setAutostop = NULL,
    .setCallback = NULL,
    .getFrequency = NULL,
    .setFrequency = NULL,
    .getOverflow = NULL,
    .setOverflow = tmrSetOverflow,
    .getValue = NULL,
    .setValue = NULL
};
/*----------------------------------------------------------------------------*/
static inline uint32_t getMaxValue(const struct SgpioBusTimer *timer)
{
  return MASK(timer->base.resolution);
}
/*----------------------------------------------------------------------------*/
static void setupChannels(struct SgpioBusTimer *timer, uint8_t match)
{
  uint8_t mask = 0;

  timer->match = match;
  mask |= 1 << timer->match;

  timer->reset = gpTimerAllocateChannel(mask);
}
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *object, const void *configPtr)
{
  const struct SgpioBusTimerConfig * const config = configPtr;
  const struct GpTimerBaseConfig baseConfig = {
      .channel = config->channel
  };
  struct SgpioBusTimer * const timer = object;
  enum Result res;

  /* Call base class constructor */
  if ((res = GpTimerBase->init(object, &baseConfig)) != E_OK)
    return res;

  /* Configure timer channels */
  setupChannels(timer, config->match);

  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->TCR = TCR_CRES; /* Timer is disabled by default */
  reg->CCR = 0;
  reg->CTCR = CTCR_INPUT(config->capture) | MODE_TOGGLE;
  reg->MCR = MCR_RESET(timer->reset);
  reg->PR = 0;

  /* Clear all pending interrupts */
  reg->IR = IR_MATCH_MASK | IR_CAPTURE_MASK;

  /* Configure match channels */
  reg->EMR = EMR_CONTROL(timer->match, CONTROL_TOGGLE);
  reg->MR[timer->reset] = getMaxValue(timer);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void tmrDeinit(void *object)
{
  struct SgpioBusTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->TCR = 0;
  GpTimerBase->deinit(timer);
}
/*----------------------------------------------------------------------------*/
static void tmrEnable(void *object)
{
  struct SgpioBusTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->TCR = TCR_CRES;
  reg->IR = IR_MATCH_MASK;
  reg->TCR = TCR_CEN;
}
/*----------------------------------------------------------------------------*/
static void tmrDisable(void *object)
{
  struct SgpioBusTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->TCR = TCR_CRES;
  reg->IR = IR_MATCH_MASK;
  reg->EMR = EMR_CONTROL(timer->match, CONTROL_TOGGLE);
}
/*----------------------------------------------------------------------------*/
static void tmrSetOverflow(void *object, uint32_t overflow)
{
  struct SgpioBusTimer * const timer = object;
  LPC_TIMER_Type * const reg = timer->base.reg;

  reg->MR[timer->match] = 1;
  reg->MR[timer->reset] = overflow;
}
