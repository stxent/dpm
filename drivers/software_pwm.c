/*
 * software_pwm.c
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/software_pwm.h>
#include <halm/generic/pointer_list.h>
#include <halm/irq.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
#define FREQUENCY_MULTIPLIER 2
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
/*----------------------------------------------------------------------------*/
static enum Result unitInit(void *, const void *);
static void unitDeinit(void *);
static void unitEnable(void *);
static void unitDisable(void *);
static uint32_t unitGetFrequency(const void *);
static void unitSetFrequency(void *, uint32_t);
static uint32_t unitGetOverflow(const void *);
static void unitSetOverflow(void *, uint32_t);

static enum Result channelInit(void *, const void *);
static void channelDeinit(void *);
static void channelEnable(void *);
static void channelDisable(void *);
static void channelSetDuration(void *, uint32_t);
static void channelSetEdges(void *, uint32_t, uint32_t);
/*----------------------------------------------------------------------------*/
const struct TimerClass * const SoftwarePwmUnit = &(const struct TimerClass){
    .size = sizeof(struct SoftwarePwmUnit),
    .init = unitInit,
    .deinit = unitDeinit,

    .enable = unitEnable,
    .disable = unitDisable,
    .setAutostop = NULL,
    .setCallback = NULL,
    .getFrequency = unitGetFrequency,
    .setFrequency = unitSetFrequency,
    .getOverflow = unitGetOverflow,
    .setOverflow = unitSetOverflow,
    .getValue = NULL,
    .setValue = NULL
};

const struct PwmClass * const SoftwarePwm = &(const struct PwmClass){
    .size = sizeof(struct SoftwarePwm),
    .init = channelInit,
    .deinit = channelDeinit,

    .enable = channelEnable,
    .disable = channelDisable,
    .setDuration = channelSetDuration,
    .setEdges = channelSetEdges
};
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct SoftwarePwmUnit * const unit = object;
  const uint32_t iteration = unit->iteration;
  PointerListNode *current = pointerListFront(&unit->channels);

  if (++unit->iteration >= unit->resolution)
    unit->iteration = 0;

  while (current != NULL)
  {
    const struct SoftwarePwm * const pwm = *pointerListData(current);

    pinWrite(pwm->pin, pwm->enabled && iteration < pwm->duration);
    current = pointerListNext(current);
  }
}
/*----------------------------------------------------------------------------*/
static enum Result unitInit(void *object, const void *configBase)
{
  const struct SoftwarePwmUnitConfig * const config = configBase;
  struct SoftwarePwmUnit * const unit = object;

  pointerListInit(&unit->channels);

  unit->iteration = 0;
  unit->resolution = config->resolution;
  unit->timer = config->timer;

  timerSetCallback(unit->timer, interruptHandler, unit);
  timerSetFrequency(unit->timer, FREQUENCY_MULTIPLIER * config->frequency);
  timerSetOverflow(unit->timer, FREQUENCY_MULTIPLIER);
  timerEnable(unit->timer);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void unitDeinit(void *object)
{
  struct SoftwarePwmUnit * const unit = object;

  timerDisable(unit->timer);
  timerSetCallback(unit->timer, NULL, NULL);
  pointerListDeinit(&unit->channels);
}
/*----------------------------------------------------------------------------*/
static void unitEnable(void *object)
{
  struct SoftwarePwmUnit * const unit = object;
  timerEnable(unit->timer);
}
/*----------------------------------------------------------------------------*/
static void unitDisable(void *object)
{
  struct SoftwarePwmUnit * const unit = object;
  timerDisable(unit->timer);
}
/*----------------------------------------------------------------------------*/
static uint32_t unitGetFrequency(const void *object)
{
  const struct SoftwarePwmUnit * const unit = object;
  return timerGetFrequency(unit->timer) / FREQUENCY_MULTIPLIER;
}
/*----------------------------------------------------------------------------*/
static void unitSetFrequency(void *object, uint32_t frequency)
{
  struct SoftwarePwmUnit * const unit = object;
  timerSetFrequency(unit->timer, frequency * FREQUENCY_MULTIPLIER);
}
/*----------------------------------------------------------------------------*/
static uint32_t unitGetOverflow(const void *object)
{
  const struct SoftwarePwmUnit * const unit = object;
  return unit->resolution;
}
/*----------------------------------------------------------------------------*/
static void unitSetOverflow(void *object, uint32_t overflow)
{
  struct SoftwarePwmUnit * const unit = object;
  unit->resolution = overflow;
}
/*----------------------------------------------------------------------------*/
static enum Result channelInit(void *object, const void *configBase)
{
  const struct SoftwarePwmConfig * const config = configBase;
  assert(config->parent != NULL);

  struct SoftwarePwm * const pwm = object;
  struct SoftwarePwmUnit * const unit = config->parent;

  pwm->pin = pinInit(config->pin);
  assert(pinValid(pwm->pin));
  pinOutput(pwm->pin, 0);

  pwm->unit = unit;
  pwm->enabled = false;
  pwm->duration = 0;

  const IrqState state = irqSave();
  pointerListPushBack(&unit->channels, pwm);
  irqRestore(state);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void channelDeinit(void *object)
{
  struct SoftwarePwmUnit * const unit = ((struct SoftwarePwm *)object)->unit;

  const IrqState state = irqSave();
  assert(pointerListFind(&unit->channels, object));
  pointerListErase(&unit->channels, object);
  irqRestore(state);
}
/*----------------------------------------------------------------------------*/
static void channelEnable(void *object)
{
  struct SoftwarePwm * const pwm = object;
  pwm->enabled = true;
}
/*----------------------------------------------------------------------------*/
static void channelDisable(void *object)
{
  struct SoftwarePwm * const pwm = object;
  pwm->enabled = false;
}
/*----------------------------------------------------------------------------*/
static void channelSetDuration(void *object, uint32_t duration)
{
  struct SoftwarePwm * const pwm = object;
  const uint32_t resolution = pwm->unit->resolution;

  pwm->duration = duration <= resolution ? duration : resolution;
}
/*----------------------------------------------------------------------------*/
static void channelSetEdges(void *object, [[maybe_unused]] uint32_t leading,
    uint32_t trailing)
{
  assert(leading == 0);
  channelSetDuration(object, trailing);
}
/*----------------------------------------------------------------------------*/
/**
 * Create a single‑edge software PWM channel.
 *
 * Initializes a new software PWM channel with single‑edge modulation.
 * The channel generates a PWM signal where only one edge is actively
 * controlled in software; the other edge is handled by timing logic.
 *
 * @param unit Pointer to a SoftwarePwmUnit object that manages
 * the PWM channels. Must not be NULL.
 * @param pin Pin used as the signal output for the PWM channel.
 * The pin must be configurable for digital output.
 * @return Pointer to a newly created SoftwarePwm object on success.
 * Returns NULL if the operation fails.
 */
void *softwarePwmCreate(void *unit, PinNumber pin)
{
  const struct SoftwarePwmConfig channelConfig = {
      .parent = unit,
      .pin = pin
  };

  return init(SoftwarePwm, &channelConfig);
}
