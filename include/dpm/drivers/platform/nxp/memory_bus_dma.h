/*
 * drivers/platform/nxp/memory_bus_dma.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_H_
#define DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_H_
/*----------------------------------------------------------------------------*/
#include <halm/dma.h>
#include <halm/irq.h>
#include <halm/pin.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const MemoryBusDma;
/*----------------------------------------------------------------------------*/
struct MemoryBusDmaFinalizer;
struct MemoryBusDmaTimer;
/*----------------------------------------------------------------------------*/
struct MemoryBusDmaConfig
{
  /** Mandatory: period of a memory cycle in timer ticks. */
  uint32_t cycle;
  /** Mandatory: pointer to an array of data output pins. */
  const pinNumber *pins;
  /** Optional: interrupt priority. */
  irqPriority priority;

  struct {
    /** Mandatory: memory control signal. */
    pinNumber leading;
    /** Mandatory: memory control signal. */
    pinNumber trailing;
    /** Mandatory: timer peripheral identifier. */
    uint8_t channel;
    /** Mandatory: DMA channel for data transfers. */
    uint8_t dma;
    /** Mandatory: enable inversion of signal. */
    bool inversion;
  } clock;

  struct {
    /** Mandatory: clock capture pin. */
    pinNumber capture;
    /** Mandatory: memory control signal. */
    pinNumber leading;
    /** Mandatory: memory control signal. */
    pinNumber trailing;
    /** Mandatory: timer peripheral identifier. */
    uint8_t channel;
    /** Mandatory: DMA channel for control transfers. */
    uint8_t dma;
    /** Mandatory: enable inversion of signal. */
    bool inversion;
  } control;
};
/*----------------------------------------------------------------------------*/
struct MemoryBusDma
{
  struct Interface parent;

  void (*callback)(void *);
  void *callbackArgument;

  struct Dma *dma;
  struct MemoryBusDmaFinalizer *finalizer;
  struct MemoryBusDmaTimer *control;
  struct MemoryBusDmaTimer *clock;

  void *gpioAddress;

  /* Bus width in power of two */
  uint8_t width;
  /* Transmission is currently active */
  bool active;
  /* Selection between blocking mode and zero copy mode */
  bool blocking;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_H_ */
