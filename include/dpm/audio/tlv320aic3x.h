/*
 * audio/tlv320aic3x.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_AUDIO_TLV320AIC3X_H_
#define DPM_AUDIO_TLV320AIC3X_H_
/*----------------------------------------------------------------------------*/
#include <dpm/audio/codec.h>
#include <halm/pin.h>
/*----------------------------------------------------------------------------*/
extern const struct CodecClass * const TLV320AIC3x;

struct Timer;

enum [[gnu::packed]] AIC3xPath
{
  AIC3X_NONE,

  AIC3X_DEFAULT_OUTPUT,
  AIC3X_LINE_OUT = AIC3X_DEFAULT_OUTPUT,
  AIC3X_LINE_OUT_DIFF,
  AIC3X_HP_COM,
  AIC3X_HP_OUT,
  AIC3X_HP_OUT_DIFF,

  AIC3X_DEFAULT_INPUT,
  AIC3X_LINE_1_IN = AIC3X_DEFAULT_INPUT,
  /* Available on TLV320AIC3101/3104 */
  AIC3X_LINE_1_IN_DIFF,
  AIC3X_LINE_2_IN,
  AIC3X_LINE_3_IN,
  /* Available on TLV320AIC3105 */
  AIC3X_MIC_1_IN,
  /* Available on TLV320AIC3101/3104 */
  AIC3X_MIC_1_IN_DIFF,
  AIC3X_MIC_2_IN,
  /* Available on TLV320AIC3105 */
  AIC3X_MIC_3_IN,

  AIC3X_END
};

struct TLV320AIC3xConfig
{
  /** Mandatory: management interface. */
  struct Interface *bus;
  /** Mandatory: timer instance for delays and watchdogs. */
  struct Timer *timer;
  /** Optional: codec address. */
  uint32_t address;
  /** Optional: codec management interface rate. */
  uint32_t rate;
  /** Optional: use master clock prescaler instead of PLL. */
  uint16_t prescaler;
  /** Mandatory: codec reset enable pin. */
  PinNumber reset;
};

struct TLV320AIC3x
{
  struct Codec base;

  void (*errorCallback)(void *);
  void *errorCallbackArgument;
  void (*idleCallback)(void *);
  void *idleCallbackArgument;
  void (*updateCallback)(void *);
  void *updateCallbackArgument;

  struct Interface *bus;
  struct Timer *timer;
  struct WorkQueue *wq;

  struct Pin reset;
  uint32_t address;
  uint32_t rate;
  bool pending;
  bool ready;

  struct
  {
    size_t length;
    uint8_t buffer[4];
    uint8_t page[2];
    uint8_t groups;
    uint8_t passed;
    uint8_t state;
    uint8_t step;
  } transfer;

  struct
  {
    uint32_t rate;

    struct
    {
      uint16_t d;
      uint8_t j;
      uint8_t p;
      uint8_t r;

      uint8_t q;
    } pll;

    struct
    {
      uint8_t path;

      uint8_t gainL;
      uint8_t gainR;
      uint8_t maxGainL;
      uint8_t maxGainR;

      uint8_t agcControlA;
      uint8_t agcControlC;
      bool agc;
    } input;

    struct
    {
      uint8_t path;

      uint8_t gainL;
      uint8_t gainR;
    } output;
  } config;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void aic3xSetAGCNoiseLevel(struct TLV320AIC3x *, int);
void aic3xSetAGCTargetLevel(struct TLV320AIC3x *, int);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_AUDIO_TLV320AIC3X_H_ */
