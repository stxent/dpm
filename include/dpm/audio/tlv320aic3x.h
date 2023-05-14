/*
 * audio/tlv320aic3x.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_AUDIO_TLV320AIC3X_H_
#define DPM_AUDIO_TLV320AIC3X_H_
/*----------------------------------------------------------------------------*/
#include <halm/pin.h>
#include <xcore/entity.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const TLV320AIC3x;

struct Timer;
struct WorkQueue;

enum AIC3xPath
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
  /** Mandatory: codec reset enable pin. */
  PinNumber reset;
};

struct TLV320AIC3x
{
  struct Entity base;

  void (*errorCallback)(void *);
  void *errorCallbackArgument;
  void (*updateCallback)(void *);
  void *updateCallbackArgument;

  struct Interface *bus;
  struct Timer *timer;
  struct WorkQueue *wq;

  struct Pin reset;
  uint32_t address;
  uint32_t rate;
  bool pending;

  struct
  {
    size_t length;
    uint8_t buffer[3];
    uint8_t page[2];
    uint8_t groups;
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
    } pll;

    struct
    {
      uint8_t path;

      uint8_t gainL;
      uint8_t gainR;
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

int aic3xGetInputGain(const struct TLV320AIC3x *);
int aic3xGetOutputGain(const struct TLV320AIC3x *);
void aic3xReset(struct TLV320AIC3x *, uint32_t, enum AIC3xPath, uint8_t, bool,
    enum AIC3xPath, uint8_t);
void aic3xSetInputGain(struct TLV320AIC3x *, uint8_t, bool);
void aic3xSetInputGainLR(struct TLV320AIC3x *, uint8_t, uint8_t, bool);
void aic3xSetInputPath(struct TLV320AIC3x *, enum AIC3xPath);
void aic3xSetOutputGain(struct TLV320AIC3x *, uint8_t);
void aic3xSetOutputGainLR(struct TLV320AIC3x *, uint8_t, uint8_t);
void aic3xSetOutputPath(struct TLV320AIC3x *, enum AIC3xPath);
void aic3xSetRate(struct TLV320AIC3x *, uint32_t);
void aic3xSetErrorCallback(struct TLV320AIC3x *, void (*)(void *), void *);
void aic3xSetUpdateCallback(struct TLV320AIC3x *, void (*)(void *), void *);
void aic3xSetUpdateWorkQueue(struct TLV320AIC3x *, struct WorkQueue *);
bool aic3xUpdate(void *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_AUDIO_TLV320AIC3X_H_ */
