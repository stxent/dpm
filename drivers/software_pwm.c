/*
 * software_pwm.c
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <halm/irq.h>
#include <dpm/drivers/software_pwm.h>
/*----------------------------------------------------------------------------*/
#define FREQUENCY_MULTIPLIER 2
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static enum result updateFrequency(struct SoftwarePwmUnit *, uint32_t);
/*----------------------------------------------------------------------------*/
static enum result unitInit(void *, const void *);
static void unitDeinit(void *);
/*----------------------------------------------------------------------------*/
static enum result channelInit(void *, const void *);
static void channelDeinit(void *);
static uint32_t channelGetResolution(const void *);
static void channelSetDuration(void *, uint32_t);
static void channelSetEdges(void *, uint32_t, uint32_t);
static void channelSetEnabled(void *, bool);
static enum result channelSetFrequency(void *, uint32_t);
/*----------------------------------------------------------------------------*/
static const struct EntityClass unitTable = {
    .size = sizeof(struct SoftwarePwmUnit),
    .init = unitInit,
    .deinit = unitDeinit
};
/*----------------------------------------------------------------------------*/
static const struct PwmClass channelTable = {
    .size = sizeof(struct SoftwarePwm),
    .init = channelInit,
    .deinit = channelDeinit,

    .getResolution = channelGetResolution,
    .setDuration = channelSetDuration,
    .setEdges = channelSetEdges,
    .setEnabled = channelSetEnabled,
    .setFrequency = channelSetFrequency
};
/*----------------------------------------------------------------------------*/
const struct EntityClass * const SoftwarePwmUnit = &unitTable;
const struct PwmClass * const SoftwarePwm = &channelTable;
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct SoftwarePwmUnit * const unit = object;
  const uint32_t iteration = unit->iteration;
  struct ListNode *current = listFirst(&unit->channels);
  struct SoftwarePwm *pwm;

  if (++unit->iteration >= unit->resolution)
    unit->iteration = 0;

  while (current)
  {
    listData(&unit->channels, current, &pwm);
    pinWrite(pwm->pin, pwm->enabled && iteration > pwm->duration);
    current = listNext(current);
  }
}
/*----------------------------------------------------------------------------*/
static enum result updateFrequency(struct SoftwarePwmUnit *unit,
    uint32_t frequency)
{
  return timerSetFrequency(unit->timer,
      FREQUENCY_MULTIPLIER * frequency * unit->resolution);
}
/*----------------------------------------------------------------------------*/
static enum result unitInit(void *object, const void *configBase)
{
  const struct SoftwarePwmUnitConfig * const config = configBase;
  struct SoftwarePwmUnit * const unit = object;
  enum result res;

  res = listInit(&unit->channels, sizeof(struct SoftwarePwm *));
  if (res != E_OK)
    return res;

  unit->iteration = 0;
  unit->resolution = config->resolution;
  unit->timer = config->timer;

  if ((res = updateFrequency(unit, config->frequency)) != E_OK)
    return res;
  if ((res = timerSetOverflow(unit->timer, FREQUENCY_MULTIPLIER)) != E_OK)
    return res;
  timerCallback(unit->timer, interruptHandler, unit);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void unitDeinit(void *object)
{
  struct SoftwarePwmUnit * const unit = object;

  timerCallback(unit->timer, 0, 0);
  listDeinit(&unit->channels);
}
/*----------------------------------------------------------------------------*/
static enum result channelInit(void *object, const void *configBase)
{
  const struct SoftwarePwmConfig * const config = configBase;
  struct SoftwarePwm * const pwm = object;
  struct SoftwarePwmUnit * const unit = config->parent;
  irqState state;

  pwm->pin = pinInit(config->pin);
  assert(pinValid(pwm->pin));
  pinOutput(pwm->pin, 0);

  pwm->unit = unit;
  pwm->enabled = false;
  pwm->duration = 0;

  state = irqSave();
  listPush(&unit->channels, &pwm);
  irqRestore(state);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void channelDeinit(void *object)
{
  struct SoftwarePwmUnit * const unit = ((struct SoftwarePwm *)object)->unit;

  const irqState state = irqSave();
  struct ListNode * const node = listFind(&unit->channels, &object);

  if (node)
    listErase(&unit->channels, node);

  irqRestore(state);
}
/*----------------------------------------------------------------------------*/
static uint32_t channelGetResolution(const void *object)
{
  const struct SoftwarePwm * const pwm = object;

  return pwm->unit->resolution;
}
/*----------------------------------------------------------------------------*/
static void channelSetDuration(void *object, uint32_t duration)
{
  struct SoftwarePwm * const pwm = object;
  const uint32_t resolution = pwm->unit->resolution;

  pwm->duration = duration <= resolution ? duration : resolution;
}
/*----------------------------------------------------------------------------*/
static void channelSetEdges(void *object,
    uint32_t leading __attribute__((unused)), uint32_t trailing)
{
  assert(leading == 0);

  channelSetDuration(object, trailing);
}
/*----------------------------------------------------------------------------*/
static void channelSetEnabled(void *object, bool state)
{
  struct SoftwarePwm * const pwm = object;

  pwm->enabled = state;
}
/*----------------------------------------------------------------------------*/
static enum result channelSetFrequency(void *object, uint32_t frequency)
{
  struct SoftwarePwm * const pwm = object;

  return updateFrequency(pwm->unit, frequency);
}
/*----------------------------------------------------------------------------*/
/**
 * Create single edge software PWM channel.
 * @param unit Pointer to a SoftwarePwmUnit object.
 * @param pin Pin used as a signal output.
 * @return Pointer to a new SoftwarePwm object on success or zero on error.
 */
void *softwarePwmCreate(void *unit, pinNumber pin)
{
  const struct SoftwarePwmConfig channelConfig = {
      .parent = unit,
      .pin = pin
  };

  return init(SoftwarePwm, &channelConfig);
}
