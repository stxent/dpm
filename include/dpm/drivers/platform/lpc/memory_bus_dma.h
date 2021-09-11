/*
 * drivers/platform/lpc/memory_bus_dma.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_DMA_H_
#define DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_DMA_H_
/*----------------------------------------------------------------------------*/
#include <halm/dma.h>
#include <halm/irq.h>
#include <halm/pin.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
struct MemoryBusDmaFinalizer;
struct MemoryBusDmaTimer;
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const MemoryBusDma;

struct MemoryBusDmaConfig
{
  /** Mandatory: period of a memory cycle in timer ticks. */
  uint32_t cycle;
  /** Mandatory: pointer to an array of data output pins. */
  const PinNumber *pins;
  /** Optional: interrupt priority. */
  IrqPriority priority;

  struct {
    /** Mandatory: leading memory control signal. */
    PinNumber leading;
    /** Mandatory: trailing memory control signal. */
    PinNumber trailing;
    /** Mandatory: timer peripheral identifier. */
    uint8_t channel;
    /** Mandatory: DMA channel for data transfers. */
    uint8_t dma;
    /** Optional: enable inversion of signal. */
    bool inversion;
    /** Optional: swap leading and trailing DMA events. */
    bool swap;
  } clock;

  struct {
    /** Mandatory: clock capture pin. */
    PinNumber capture;
    /** Mandatory: leading memory control signal. */
    PinNumber leading;
    /** Mandatory: trailing memory control signal. */
    PinNumber trailing;
    /** Mandatory: timer peripheral identifier. */
    uint8_t channel;
    /** Mandatory: DMA channel for control transfers. */
    uint8_t dma;
    /** Optional: enable inversion of signal. */
    bool inversion;
  } control;
};

struct MemoryBusDma
{
  struct Interface base;

  void (*callback)(void *);
  void *callbackArgument;

  struct Dma *dma;
  struct MemoryBusDmaFinalizer *finalizer;
  struct MemoryBusDmaTimer *clock;
  struct MemoryBusDmaTimer *control;

  void *address;

  /* Bus width in power of two */
  uint8_t width;
  /* Transmission is currently active */
  bool active;
  /* Selection between blocking mode and zero copy mode */
  bool blocking;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_DMA_H_ */
