/*
 * memory/m24.h
 * Copyright (C) 2019, 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_M24_H_
#define DPM_MEMORY_M24_H_
/*----------------------------------------------------------------------------*/
#include <xcore/interface.h>
#include <stdbool.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const M24;

struct Timer;
struct WorkQueue;

struct M24Config
{
  /** Mandatory: serial interface. */
  struct Interface *bus;
  /** Mandatory: timer instance for delays and watchdogs. */
  struct Timer *timer;
  /** Mandatory: bus address. */
  uint32_t address;
  /** Mandatory: address space size. */
  uint32_t chipSize;
  /** Mandatory: page size. */
  uint32_t pageSize;
  /** Optional: baud rate of the interface. */
  uint32_t rate;
  /** Mandatory: block count. */
  uint8_t blocks;
};

struct M24
{
  struct Interface base;

  /* Interface callbacks */
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

  uint32_t address;
  uint32_t rate;

  uint32_t chipSize;
  uint16_t pageSize;
  uint8_t shift;
  uint8_t width;

  struct
  {
    uint8_t *buffer;
    uint8_t *rxBuffer;
    const uint8_t *txBuffer;

    size_t chunk;
    size_t count;
    uint32_t position;

    /* Current transfer state */
    uint8_t state;
    /* Operation result */
    uint8_t status;
  } transfer;

  /* Enable blocking mode */
  bool blocking;
  /* State update is requested */
  bool pending;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void m24SetErrorCallback(void *, void (*)(void *), void *);
void m24SetUpdateCallback(void *, void (*)(void *), void *);
void m24SetUpdateWorkQueue(void *, struct WorkQueue *);

bool m24Update(void *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_M24_H_ */
