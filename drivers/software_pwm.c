/*
 * software_pwm.c
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <error.h>
#include <drivers/software_pwm.h>
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
    pinWrite(pwm->pin, iteration > pwm->duration);
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

  pwm->pin = pinInit(config->pin);
  if (!pinValid(pwm->pin))
  {
    /* Pin does not exist or cannot be used */
    return E_VALUE;
  }
  pinOutput(pwm->pin, 0);

  pwm->unit = unit;
  pwm->duration = config->duration;

  timerSetEnabled(unit->timer, false);
  listPush(&unit->channels, &pwm);
  timerSetEnabled(unit->timer, true);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void channelDeinit(void *object)
{
  struct SoftwarePwmUnit * const unit = ((struct SoftwarePwm *)object)->unit;

  timerSetEnabled(unit->timer, false);

  struct ListNode * const node = listFind(&unit->channels, &object);

  if (node)
    listErase(&unit->channels, node);

  timerSetEnabled(unit->timer, true);
}
/*----------------------------------------------------------------------------*/
static uint32_t channelGetResolution(const void *object)
{
  const struct SoftwarePwmUnit * const unit =
      ((struct SoftwarePwm *)object)->unit;

  return unit->resolution;
}
/*----------------------------------------------------------------------------*/
static void channelSetDuration(void *object, uint32_t duration)
{
  struct SoftwarePwm * const pwm = object;

  pwm->duration = duration;
}
/*----------------------------------------------------------------------------*/
static void channelSetEdges(void *object, uint32_t leading, uint32_t trailing)
{
  struct SoftwarePwm * const pwm = object;

  assert(leading == 0);
  pwm->duration = trailing;
}
/*----------------------------------------------------------------------------*/
static void channelSetEnabled(void *object, bool state)
{
  //TODO
}
/*----------------------------------------------------------------------------*/
static enum result channelSetFrequency(void *object, uint32_t frequency)
{
  struct SoftwarePwmUnit * const unit = ((struct SoftwarePwm *)object)->unit;

  return updateFrequency(unit, frequency);
}
/*----------------------------------------------------------------------------*/
void *softwarePwmCreate(void *unit, pinNumber pin, uint32_t duration)
{
  const struct SoftwarePwmConfig channelConfig = {
      .parent = unit,
      .duration = duration,
      .pin = pin
  };

  return init(SoftwarePwm, &channelConfig);
}
