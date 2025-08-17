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

enum [[gnu::packed]] TEA57XXClockSource
{
  TEA57XX_CLOCK_32K,
  TEA57XX_CLOCK_6M5,
  TEA57XX_CLOCK_13M,

  TEA57XX_CLOCK_END
};

enum [[gnu::packed]] TEA57XXSearchLevel
{
  TEA57XX_SEARCH_DEFAULT,
  TEA57XX_SEARCH_COARSE,
  TEA57XX_SEARCH_MEDIUM,
  TEA57XX_SEARCH_FINE,

  TEA57XX_SEARCH_END
};

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
  /** Mandatory: input clock type. */
  enum TEA57XXClockSource clock;
  /** Optional: search sensitivity level. */
  enum TEA57XXSearchLevel sensitivity;
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

  /* Input clock settings */
  enum TEA57XXClockSource clock;
  /* Search sensitivity settings */
  enum TEA57XXSearchLevel sensitivity;

  /* Current frequency */
  uint32_t frequency;
  /* Current signal level */
  uint8_t level;
  /* Search mode enabled */
  bool search;

  /* Temporary buffer for configuration and status registers */
  uint8_t buffer[5];
  /* Current configuration */
  uint8_t config[5];
  /* Command and status flags */
  uint8_t flags;
  /* Current operation */
  uint8_t state;
  /* State update is requested */
  bool pending;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void tea57xxSetCallback(void *, void (*)(void *), void *);
void tea57xxSetErrorCallback(void *, void (*)(void *), void *);
void tea57xxSetUpdateCallback(void *, void (*)(void *), void *);
void tea57xxSetUpdateWorkQueue(void *, struct WorkQueue *);

bool tea57xxUpdate(void *);

uint32_t tea57xxGetFrequency(const struct TEA57XX *);
uint32_t tea57xxGetLevel(const struct TEA57XX *);
bool tea57xxIsMuted(const struct TEA57XX *);
bool tea57xxIsSearching(const struct TEA57XX *);
void tea57xxRequestState(struct TEA57XX *);
void tea57xxReset(struct TEA57XX *);
void tea57xxSearch(struct TEA57XX *, bool);
void tea57xxSetFrequency(struct TEA57XX *, uint32_t);
void tea57xxSetMute(struct TEA57XX *, enum CodecChannel);
void tea57xxSuspend(struct TEA57XX *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_RADIO_TEA57XX_H_ */
