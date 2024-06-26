/*
 * memory_bus_dma_finalizer.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/memory_bus_dma_finalizer.h>
#include <halm/platform/lpc/gpdma_oneshot.h>
#include <halm/platform/lpc/gptimer_defs.h>
#include <xcore/memory.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static enum Result finalizerInit(void *, const void *);
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
enum Result memoryBusDmaFinalizerStart(struct MemoryBusDmaFinalizer *finalizer)
{
  LPC_TIMER_Type * const reg = finalizer->sender->base.reg;

  dmaAppend(finalizer->dma, (void *)&reg->MCR, &finalizer->value,
      sizeof(finalizer->value));
  return dmaEnable(finalizer->dma);
}
/*----------------------------------------------------------------------------*/
void memoryBusDmaFinalizerStop(struct MemoryBusDmaFinalizer *finalizer)
{
  dmaDisable(finalizer->dma);
}
/*----------------------------------------------------------------------------*/
static enum Result finalizerInit(void *object, const void *configPtr)
{
  const struct MemoryBusDmaFinalizerConfig * const config = configPtr;
  assert(config != NULL);
  assert(config->marshal != NULL && config->sender != NULL);

  struct MemoryBusDmaFinalizer * const finalizer = object;

  finalizer->marshal = config->marshal;
  finalizer->sender = config->sender;

  const uint8_t matchChannel = finalizer->marshal->trailing;
  const uint8_t resetChannel = finalizer->sender->reset;

  /* Only channels 0 and 1 can be used as DMA events */
  assert(matchChannel < 2);

  static const struct GpDmaSettings dmaSettings = {
      .source = {
          .burst = DMA_BURST_1,
          .width = DMA_WIDTH_WORD,
          .increment = false
      },
      .destination = {
          .burst = DMA_BURST_1,
          .width = DMA_WIDTH_WORD,
          .increment = false
      }
  };
  const struct GpDmaOneShotConfig dmaConfig = {
      .event = GPDMA_MAT0_0 + matchChannel
          + finalizer->marshal->base.channel * 2,
      .type = GPDMA_TYPE_M2P,
      .channel = config->channel
  };

  finalizer->dma = init(GpDmaOneShot, &dmaConfig);

  if (finalizer->dma != NULL)
  {
    dmaConfigure(finalizer->dma, &dmaSettings);

    finalizer->value = MCR_INTERRUPT(resetChannel)
        | MCR_RESET(resetChannel)
        | MCR_STOP(resetChannel);

    return E_OK;
  }
  else
    return E_ERROR;
}
/*----------------------------------------------------------------------------*/
static void finalizerDeinit(void *object)
{
  struct MemoryBusDmaFinalizer * const finalizer = object;
  deinit(finalizer->dma);
}
