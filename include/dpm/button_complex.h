/*
 * button_complex.h
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_BUTTON_COMPLEX_H_
#define DPM_BUTTON_COMPLEX_H_
/*----------------------------------------------------------------------------*/
#include <halm/pin.h>
#include <xcore/entity.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const ButtonComplex;

struct Interrupt;
struct Timer;

struct ButtonComplexConfig
{
  /** Mandatory: pin interrupt. */
  struct Interrupt *interrupt;
  /** Mandatory: tick timer, timer will be configured for 100 Hz tick rate. */
  struct Timer *timer;
  /** Mandatory: input pin. */
  PinNumber pin;
  /** Optional: debouncing delay in timer ticks. */
  unsigned short delay;
  /** Optional: long press delay in timer ticks. */
  unsigned short hold;
  /** Mandatory: active level. */
  bool level;
};

struct ButtonComplex
{
  struct Entity base;

  void (*longPressCallback)(void *);
  void *longPressCallbackArgument;
  void (*pressCallback)(void *);
  void *pressCallbackArgument;
  void (*releaseCallback)(void *);
  void *releaseCallbackArgument;

  struct Interrupt *interrupt;
  struct Timer *timer;
  struct Pin pin;

  unsigned short counter;
  unsigned short delayHold;
  unsigned short delayWait;
  bool level;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void buttonComplexEnable(struct ButtonComplex *);
void buttonComplexDisable(struct ButtonComplex *);
void buttonComplexSetLongPressCallback(struct ButtonComplex *,
    void (*)(void *), void *);
void buttonComplexSetPressCallback(struct ButtonComplex *,
    void (*)(void *), void *);
void buttonComplexSetReleaseCallback(struct ButtonComplex *,
    void (*)(void *), void *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_BUTTON_COMPLEX_H_ */
