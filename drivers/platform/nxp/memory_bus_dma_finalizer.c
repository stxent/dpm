/*
 * memory_bus_dma_finalizer.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <halm/platform/nxp/gpdma.h>
#include <halm/platform/nxp/gptimer_defs.h>
#include <xcore/memory.h>
#include <dpm/drivers/platform/nxp/memory_bus_dma_finalizer.h>
/*----------------------------------------------------------------------------*/
static enum result finalizerInit(void *, const void *);
static void finalizerDeinit(void *);
/*----------------------------------------------------------------------------*/
static const struct EntityClass finalizerTable = {
    .size = sizeof(struct MemoryBusDmaFinalizer),
    .init = finalizerInit,
    .deinit = finalizerDeinit
};
/*----------------------------------------------------------------------------*/
const struct EntityClass * const MemoryBusDmaFinalizer = &finalizerTable;
/*----------------------------------------------------------------------------*/
enum result memoryBusDmaFinalizerStart(struct MemoryBusDmaFinalizer *finalizer)
{
  LPC_TIMER_Type * const reg = finalizer->sender->parent.reg;

  return dmaStart(finalizer->dma, (void *)&reg->MCR, &finalizer->value, 1);
}
/*----------------------------------------------------------------------------*/
void memoryBusDmaFinalizerStop(struct MemoryBusDmaFinalizer *finalizer)
{
  dmaStop(finalizer->dma);
}
/*----------------------------------------------------------------------------*/
static enum result finalizerInit(void *object, const void *configPtr)
{
  const struct MemoryBusDmaFinalizerConfig * const config = configPtr;
  struct MemoryBusDmaFinalizer * const finalizer = object;

  assert(config->marshal && config->sender);

  finalizer->marshal = config->marshal;
  finalizer->sender = config->sender;

  const uint8_t matchChannel =
      memoryBusDmaTimerPrimaryChannel(finalizer->marshal);
  const uint8_t resetChannel = finalizer->sender->resetChannel;

  /* Only channels 0 and 1 can be used as DMA events */
  assert(matchChannel < 2);

  const struct GpDmaConfig dmaConfig = {
      .channel = config->channel,
      .destination.increment = false,
      .source.increment = false,
      .burst = DMA_BURST_1,
      .event = GPDMA_MAT0_0 + matchChannel
          + finalizer->marshal->parent.channel * 2,
      .type = GPDMA_TYPE_M2P,
      .width = DMA_WIDTH_WORD
  };

  finalizer->dma = init(GpDma, &dmaConfig);
  if (!finalizer->dma)
    return E_ERROR;

  finalizer->value = MCR_RESET(resetChannel) | MCR_INTERRUPT(resetChannel)
      | MCR_STOP(resetChannel);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void finalizerDeinit(void *object)
{
  struct MemoryBusDmaFinalizer * const finalizer = object;

  deinit(finalizer->dma);
}
