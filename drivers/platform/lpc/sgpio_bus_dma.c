/*
 * sgpio_bus_dma.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/sgpio_bus_dma.h>
#include <halm/platform/lpc/gpdma_defs.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static enum Result channelInit(void *, const void *);
static void channelDeinit(void *);

static void channelConfigure(void *, const void *);
static void channelSetCallback(void *, void (*)(void *), void *);

static enum Result channelEnable(void *);
static void channelDisable(void *);
static enum Result channelResidue(const void *, size_t *);
static enum Result channelStatus(const void *);

static void channelAppend(void *, void *, const void *, size_t);
static void channelClear(void *);
static size_t channelQueued(const void *);
/*----------------------------------------------------------------------------*/
const struct DmaClass * const SgpioBusDma = &(const struct DmaClass){
    .size = sizeof(struct SgpioBusDma),
    .init = channelInit,
    .deinit = channelDeinit,

    .configure = channelConfigure,
    .setCallback = channelSetCallback,

    .enable = channelEnable,
    .disable = channelDisable,
    .residue = channelResidue,
    .status = channelStatus,

    .append = channelAppend,
    .clear = channelClear,
    .queued = channelQueued
};
/*----------------------------------------------------------------------------*/
static enum Result channelInit(void *object, const void *configBase)
{
  const struct SgpioBusDmaConfig * const config = configBase;
  assert(config);

  const struct GpDmaOneShotConfig baseConfig = {
      .event = config->event,
      .type = config->type,
      .channel = config->channel
  };
  struct SgpioBusDma * const channel = object;
  enum Result res;

  /* Call base class constructor */
  if ((res = GpDmaOneShot->init(channel, &baseConfig)) != E_OK)
    return res;

  channel->masters = gpDmaBaseCalcMasterAffinity(&channel->base.base,
      config->dstMaster, config->srcMaster);
  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void channelDeinit(void *object)
{
  GpDmaOneShot->deinit(object);
}
/*----------------------------------------------------------------------------*/
static void channelConfigure(void *object, const void *settingsBase)
{
  const struct GpDmaSettings * const settings = settingsBase;
  struct SgpioBusDma * const channel = object;

  GpDmaOneShot->configure(channel, settings);
  channel->base.control = (channel->base.control & ~CONTROL_MASTER_MASK)
      | channel->masters;
}
/*----------------------------------------------------------------------------*/
static void channelSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  GpDmaOneShot->setCallback(object, callback, argument);
}
/*----------------------------------------------------------------------------*/
static enum Result channelEnable(void *object)
{
  return GpDmaOneShot->enable(object);
}
/*----------------------------------------------------------------------------*/
static void channelDisable(void *object)
{
  GpDmaOneShot->disable(object);
}
/*----------------------------------------------------------------------------*/
static enum Result channelResidue(const void *object, size_t *count)
{
  return GpDmaOneShot->residue(object, count);
}
/*----------------------------------------------------------------------------*/
static enum Result channelStatus(const void *object)
{
  return GpDmaOneShot->status(object);
}
/*----------------------------------------------------------------------------*/
static void channelAppend(void *object, void *destination, const void *source,
    size_t size)
{
  GpDmaOneShot->append(object, destination, source, size);
}
/*----------------------------------------------------------------------------*/
static void channelClear(void *object)
{
  GpDmaOneShot->clear(object);
}
/*----------------------------------------------------------------------------*/
static size_t channelQueued(const void *object)
{
  return GpDmaOneShot->queued(object);
}
