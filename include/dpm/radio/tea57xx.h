/*
 * radio/tea57xx.h
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_RADIO_TEA57XX_H_
#define DPM_RADIO_TEA57XX_H_
/*----------------------------------------------------------------------------*/
#include <dpm/audio/codec.h>
#include <xcore/entity.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const TEA57XX;

struct Interface;
struct Timer;
struct WorkQueue;

struct TEA57XXConfig
{
  /** Mandatory: I2C interface. */
  struct Interface *bus;
  /** Mandatory: timer instance for delays and watchdogs. */
  struct Timer *timer;
  /** Optional: bus address. */
  uint32_t address;
  /** Optional: baud rate of the interface. */
  uint32_t rate;
};

struct TEA57XX
{
  struct Entity base;

  /* User callbacks */
  void (*callback)(void *);
  void *callbackArgument;

  /* Bus Handler callbacks */
  void (*errorCallback)(void *);
  void *errorCallbackArgument;
  void (*idleCallback)(void *);
  void *idleCallbackArgument;
  void (*updateCallback)(void *);
  void *updateCallbackArgument;

  struct Interface *bus;
  struct Timer *timer;
  struct WorkQueue *wq;

  /* Bus interface address */
  uint32_t address;
  /* Bus interface bit rate */
  uint32_t rate;

  /* Temporary buffer for configuration and status registers */
  uint8_t buffer[5];
  /* Current configuration */
  uint8_t config[5];
  /* Command and status flags */
  uint8_t flags;
  /* Signal level */
  uint8_t level;
  /* Current operation */
  uint8_t state;

  /* Enable blocking mode */
  bool blocking;
  /* State update is requested */
  bool pending;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void tea57xxSetErrorCallback(void *, void (*)(void *), void *);
void tea57xxSetUpdateCallback(void *, void (*)(void *), void *);
void tea57xxSetUpdateWorkQueue(void *, struct WorkQueue *);

bool tea57xxUpdate(void *);

uint8_t tea57xxGetLevel(const struct TEA57XX *);
void tea57xxRequestLevel(struct TEA57XX *);
void tea57xxSetFrequency(struct TEA57XX *, uint32_t);
void tea57xxSetMute(struct TEA57XX *, enum CodecChannel);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_RADIO_TEA57XX_H_ */
