/*
 * memory_bus_gpio_timer.c
 * Copyright (C) 2019 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <halm/platform/stm/gptimer_defs.h>
#include <dpm/drivers/platform/stm/memory_bus_gpio_timer.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static enum Result setupChannels(struct MemoryBusGpioTimer *,
    const struct MemoryBusGpioTimerConfig *);

#ifdef CONFIG_PLATFORM_STM_GPTIMER_PM
static void powerStateHandler(void *, enum PmState);
#endif
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *, const void *);
static void tmrSetCallback(void *, void (*)(void *), void *);
static void tmrEnable(void *);
static void tmrDisable(void *);
static void tmrSetFrequency(void *, uint32_t);
static void tmrSetOverflow(void *, uint32_t);
static uint32_t tmrGetValue(const void *);
static void tmrSetValue(void *, uint32_t);

#ifndef CONFIG_PLATFORM_STM_GPTIMER_NO_DEINIT
static void tmrDeinit(void *);
#else
#define tmrDeinit deletedDestructorTrap
#endif
/*----------------------------------------------------------------------------*/
const struct TimerClass * const MemoryBusGpioTimer = &(const struct TimerClass){
    .size = sizeof(struct MemoryBusGpioTimer),
    .init = tmrInit,
    .deinit = tmrDeinit,

    .enable = tmrEnable,
    .disable = tmrDisable,
    .setCallback = tmrSetCallback,
    .getFrequency = 0,
    .setFrequency = tmrSetFrequency,
    .getOverflow = 0,
    .setOverflow = tmrSetOverflow,
    .getValue = tmrGetValue,
    .setValue = tmrSetValue
};
/*----------------------------------------------------------------------------*/
static inline uint32_t getMaxValue(const struct MemoryBusGpioTimer *timer)
{
  return MASK(timer->base.resolution);
}
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
#ifdef CONFIG_PLATFORM_STM_GPTIMER_PM
static void powerStateHandler(void *object, enum PmState state)
{
  if (state == PM_ACTIVE)
  {
    struct MemoryBusGpioTimer * const timer = object;
    setTimerFrequency(timer, timer->frequency);
  }
}
#endif
/*----------------------------------------------------------------------------*/
static void setTimerFrequency(struct MemoryBusGpioTimer *timer,
    uint32_t frequency)
{
  STM_TIM_Type * const reg = timer->base.reg;
  uint32_t divisor;

  if (frequency)
  {
    const uint32_t apbFrequency = gpTimerGetClock(&timer->base);
    assert(frequency <= apbFrequency);

    divisor = apbFrequency / frequency - 1;
    assert(divisor <= MASK(timer->base.resolution));
  }
  else
    divisor = 0;

  reg->PSC = divisor;
  reg->EGR = EGR_UG;
}
/*----------------------------------------------------------------------------*/
static enum Result setupChannels(struct MemoryBusGpioTimer *timer,
    const struct MemoryBusGpioTimerConfig *config)
{
  const int channel = gpTimerConfigComparePin(config->channel, config->pin);

  if (channel != -1)
  {
    timer->channel = channel;
    return E_OK;
  }
  else
    return E_VALUE;
}
/*----------------------------------------------------------------------------*/
static enum Result tmrInit(void *object, const void *configPtr)
{
  const struct MemoryBusGpioTimerConfig * const config = configPtr;
  const struct GpTimerBaseConfig parentConfig = {
      .channel = config->channel
  };
  struct MemoryBusGpioTimer * const timer = object;
  enum Result res;

  assert(config->frequency);

  /* Call base class constructor */
  if ((res = GpTimerBase->init(object, &parentConfig)) != E_OK)
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
  reg->ARR = getMaxValue(timer);
  reg->CNT = 0;
  reg->DIER = 0;

  reg->CCMR[timer->channel >> 1] = CCMR_OCPE(timer->channel & 1)
      | CCMR_OCM(timer->channel & 1, OCM_PWM_MODE_2)
      | CCMR_CCS(timer->channel & 1, CCS_OUTPUT);
  reg->CCER = ccer;

  //  reg->CR2 &= ~CR2_CCPC; // TODO Advanced timers

  tmrSetFrequency(timer, config->frequency);
  tmrSetOverflow(timer, config->cycle);

#ifdef CONFIG_PLATFORM_STM_GPTIMER_PM
  if ((res = pmRegister(powerStateHandler, timer)) != E_OK)
    return res;
#endif

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

#ifdef CONFIG_PLATFORM_STM_GPTIMER_PM
  pmUnregister(timer);
#endif

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

  if (callback)
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
  setTimerFrequency(timer, timer->frequency);
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
static void tmrSetValue(void *object __attribute__((unused)),
    uint32_t value __attribute__((unused)))
{
}
