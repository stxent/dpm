/*
 * drivers/platform/nxp/memory_bus_dma_finalizer.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_FINALIZER_H_
#define DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_FINALIZER_H_
/*----------------------------------------------------------------------------*/
#include <dma.h>
#include <drivers/platform/nxp/memory_bus_dma_timer.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const MemoryBusDmaFinalizer;
/*----------------------------------------------------------------------------*/
struct MemoryBusDmaFinalizerConfig
{
  /** Mandatory: memory bus control timer. */
  struct MemoryBusDmaTimer *marshal;
  /** Mandatory: memory bus transmission timer. */
  struct MemoryBusDmaTimer *sender;
  /** Mandatory: direct memory access channel for timer control. */
  uint8_t channel;
};
/*----------------------------------------------------------------------------*/
struct MemoryBusDmaFinalizer
{
  struct Entity parent;

  struct Dma *dma;
  struct MemoryBusDmaTimer *marshal;
  struct MemoryBusDmaTimer *sender;

  /* Precalculated timer configuration register value */
  uint32_t value;
};
/*----------------------------------------------------------------------------*/
enum result memoryBusDmaFinalizerStart(struct MemoryBusDmaFinalizer *);
void memoryBusDmaFinalizerStop(struct MemoryBusDmaFinalizer *);
/*----------------------------------------------------------------------------*/
#endif /* DRIVERS_PLATFORM_NXP_MEMORY_BUS_DMA_FINALIZER_H_ */
