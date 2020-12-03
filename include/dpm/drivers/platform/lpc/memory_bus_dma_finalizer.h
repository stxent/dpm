/*
 * drivers/platform/lpc/memory_bus_dma_finalizer.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_DMA_FINALIZER_H_
#define DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_DMA_FINALIZER_H_
/*----------------------------------------------------------------------------*/
#include <dpm/drivers/platform/lpc/memory_bus_dma_timer.h>
#include <halm/dma.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const MemoryBusDmaFinalizer;

struct MemoryBusDmaFinalizerConfig
{
  /** Mandatory: memory bus control timer. */
  struct MemoryBusDmaTimer *marshal;
  /** Mandatory: memory bus transmission timer. */
  struct MemoryBusDmaTimer *sender;
  /** Mandatory: direct memory access channel for timer control. */
  uint8_t channel;
};

struct MemoryBusDmaFinalizer
{
  struct Entity base;

  struct Dma *dma;
  struct MemoryBusDmaTimer *marshal;
  struct MemoryBusDmaTimer *sender;

  /* Precalculated timer configuration register value */
  uint32_t value;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

enum Result memoryBusDmaFinalizerStart(struct MemoryBusDmaFinalizer *);
void memoryBusDmaFinalizerStop(struct MemoryBusDmaFinalizer *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_LPC_MEMORY_BUS_DMA_FINALIZER_H_ */
