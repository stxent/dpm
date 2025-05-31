/*
 * platform/lpc/sgpio_bus_dma.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_PLATFORM_LPC_SGPIO_BUS_DMA_H_
#define DPM_PLATFORM_LPC_SGPIO_BUS_DMA_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/lpc/gpdma_oneshot.h>
/*----------------------------------------------------------------------------*/
extern const struct DmaClass * const SgpioBusDma;

struct SgpioBusDmaConfig
{
  /** Mandatory: request connection to the peripheral or memory. */
  enum GpDmaEvent event;
  /** Optional: destination master. */
  enum GpDmaMaster dstMaster;
  /** Optional: source master. */
  enum GpDmaMaster srcMaster;
  /** Mandatory: transfer type. */
  enum GpDmaType type;
  /** Mandatory: channel number. */
  uint8_t channel;
};

struct SgpioBusDma
{
  struct GpDmaOneShot base;

  /* Masters configuration for control register */
  uint32_t masters;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_PLATFORM_LPC_SGPIO_BUS_DMA_H_ */
