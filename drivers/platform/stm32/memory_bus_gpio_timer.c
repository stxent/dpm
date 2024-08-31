/*
 * memory_bus_gpio_timer.c
 * Copyright (C) 2019 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/stm32/memory_bus_gpio_timer.h>
#include <halm/platform/stm32/gptimer_defs.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static enum Result setupChannels(struct MemoryBusGpioTimer *,
    const struct MemoryBusGpioTimerConfig *);
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *, const void *);
static void tmrDeinit(void *);
static void tmrSetCallback(void *, void (*)(void *), void *);
static void tmrEnable(void *);
static void tmrDisable(void *);
static void tmrSetFrequency(void *, uint32_t);
static void tmrSetOverflow(void *, uint32_t);
static uint32_t tmrGetValue(const void *);
static void tmrSetValue(void *, uint32_t);
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
    .getValue = tmrGetValue,
    .setValue = tmrSetValue
};
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  STM_TIM_Type * const reg = timer->base.reg;

  /* Clear all pending interrupts */
  reg->SR = ~(SR_CCIF_MASK | SR_UIF);

  timer->callback(timer->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static enum Result setupChannels(struct MemoryBusGpioTimer *timer,
    const struct MemoryBusGpioTimerConfig *config)
{
  const int channel = gpTimerConfigOutputPin(config->channel, config->pin);

  if (channel != -1)
  {
    timer->channel = channel >> 1;
    return E_OK;
  }
  else
    return E_VALUE;
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

  if ((res = setupChannels(timer, config)) != E_OK)
    return res;

  /* Initialize peripheral block */
  STM_TIM_Type * const reg = timer->base.reg;
  uint32_t ccer = CCER_CCE(timer->channel);

  if (config->inversion)
    ccer |= CCER_CCP(timer->channel);

  reg->CR1 = CR1_CKD(CKD_CK_INT) | CR1_CMS(CMS_EDGE_ALIGNED_MODE);
  reg->ARR = getMaxValue(timer->base.flags);
  reg->CNT = 0;
  reg->DIER = 0;

  reg->CCMR[timer->channel >> 1] = CCMR_OCPE(timer->channel & 1)
      | CCMR_OCM(timer->channel & 1, OCM_PWM_MODE_2)
      | CCMR_CCS(timer->channel & 1, CCS_OUTPUT);
  reg->CCER = ccer;

  if (timer->base.flags & TIMER_FLAG_CONTROL)
    reg->BDTR |= BDTR_MOE;

  tmrSetFrequency(timer, config->frequency);
  tmrSetOverflow(timer, config->cycle);

  irqSetPriority(timer->base.irq, config->priority);
  irqEnable(timer->base.irq);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void tmrDeinit(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  STM_TIM_Type * const reg = timer->base.reg;

  irqDisable(timer->base.irq);
  reg->CR1 &= ~CR1_CEN;

  GpTimerBase->deinit(timer);
}
/*----------------------------------------------------------------------------*/
static void tmrSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct MemoryBusGpioTimer * const timer = object;
  STM_TIM_Type * const reg = timer->base.reg;

  timer->callbackArgument = argument;
  timer->callback = callback;

  if (timer->callback != NULL)
  {
    /* Clear pending interrupt flags */
    reg->SR = 0;
    /* Enable generation of an interrupt request */
    reg->DIER |= DIER_CCIE(timer->channel);
  }
  else
  {
    /* Disable interrupt request generation */
    reg->DIER &= ~DIER_CCIE_MASK;
  }
}
/*----------------------------------------------------------------------------*/
static void tmrEnable(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  STM_TIM_Type * const reg = timer->base.reg;

  /* Disable interrupt generation on update event */
  reg->DIER &= ~DIER_UIE;
  /* Clear pending interrupt flags */
  reg->SR = 0;
  /* Start the timer */
  reg->CR1 = (reg->CR1 & ~CR1_OPM) | CR1_CEN;
}
/*----------------------------------------------------------------------------*/
static void tmrDisable(void *object)
{
  struct MemoryBusGpioTimer * const timer = object;
  STM_TIM_Type * const reg = timer->base.reg;

  /* Generate interrupt on update event */
  reg->DIER |= DIER_UIE;
  /* Complete current operation and stop the timer */
  reg->CR1 |= CR1_OPM;
}
/*----------------------------------------------------------------------------*/
static void tmrSetFrequency(void *object, uint32_t frequency)
{
  struct MemoryBusGpioTimer * const timer = object;

  timer->frequency = frequency;
  gpTimerSetFrequency(&timer->base, timer->frequency);
}
/*----------------------------------------------------------------------------*/
static void tmrSetOverflow(void *object, uint32_t overflow)
{
  struct MemoryBusGpioTimer * const timer = object;
  STM_TIM_Type * const reg = timer->base.reg;

  assert(overflow);

  reg->CCR[timer->channel] = (overflow >> 1) - 1;
  reg->ARR = overflow - 1;
  reg->EGR = EGR_UG;
}
/*----------------------------------------------------------------------------*/
static uint32_t tmrGetValue(const void *object)
{
  const struct MemoryBusGpioTimer * const timer = object;
  const STM_TIM_Type * const reg = timer->base.reg;

  return reg->CNT;
}
/*----------------------------------------------------------------------------*/
static void tmrSetValue(void *, uint32_t)
{
}
