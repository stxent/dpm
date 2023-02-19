/*
 * dfu_bridge.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/usb/dfu_bridge.h>
#include <halm/generic/flash.h>
#include <halm/generic/work_queue.h>
#include <halm/irq.h>
#include <halm/usb/dfu.h>
#include <halm/usb/usb_trace.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
static const struct FlashGeometry *findFlashRegion(const struct DfuBridge *,
    size_t);
static void bridgeReset(struct DfuBridge *);
static void flashProgramTask(void *);
static uint32_t getSectorEraseTime(const struct DfuBridge *, size_t);
static bool isSectorAddress(const struct DfuBridge *, size_t);
static void onDetachRequest(void *, uint16_t);
static size_t onDownloadRequest(void *, size_t, const void *, size_t,
    uint16_t *);
static size_t onUploadRequest(void *, size_t, void *, size_t);
/*----------------------------------------------------------------------------*/
static enum Result bridgeInit(void *, const void *);
static void bridgeDeinit(void *);
/*----------------------------------------------------------------------------*/
const struct EntityClass * const DfuBridge = &(const struct EntityClass){
    .size = sizeof(struct DfuBridge),
    .init = bridgeInit,
    .deinit = bridgeDeinit
};
/*----------------------------------------------------------------------------*/
static const struct FlashGeometry *findFlashRegion(
    const struct DfuBridge *loader, size_t address)
{
  if (address >= loader->flashSize)
    return 0;

  const struct FlashGeometry *region = loader->geometry;
  size_t offset = 0;

  while (region->count)
  {
    /* Sector size should be a power of 2 */
    assert((region->size & (region->size - 1)) == 0);

    if (((address - offset) & (region->size - 1)) == 0)
      return region;

    offset += region->count * region->size;
    ++region;
  }

  return 0;
}
/*----------------------------------------------------------------------------*/
static void bridgeReset(struct DfuBridge *loader)
{
  loader->bufferSize = 0;
  loader->currentPosition = loader->flashOffset;
  loader->erasingPosition = 0;
  loader->eraseQueued = false;
  memset(loader->chunk, 0xFF, loader->chunkSize);
}
/*----------------------------------------------------------------------------*/
static void flashProgramTask(void *argument)
{
  struct DfuBridge * const loader = argument;

  loader->eraseQueued = false;

  const IrqState irqState = irqSave();
  ifSetParam(loader->flash, IF_FLASH_ERASE_SECTOR, &loader->erasingPosition);
  dfuOnDownloadCompleted(loader->device, true);
  irqRestore(irqState);
}
/*----------------------------------------------------------------------------*/
static uint32_t getSectorEraseTime(const struct DfuBridge *loader,
    size_t address)
{
  const struct FlashGeometry * const region = findFlashRegion(loader, address);
  return region ? region->time : 0;
}
/*----------------------------------------------------------------------------*/
static bool isSectorAddress(const struct DfuBridge *loader, size_t address)
{
  const struct FlashGeometry * const region = findFlashRegion(loader, address);
  return region != 0;
}
/*----------------------------------------------------------------------------*/
static void onDetachRequest(void *object,
    uint16_t timeout __attribute__((unused)))
{
  struct DfuBridge * const loader = object;
  loader->reset();
}
/*----------------------------------------------------------------------------*/
static size_t onDownloadRequest(void *object, size_t position,
    const void *buffer, size_t length, uint16_t *timeout)
{
  struct DfuBridge * const loader = object;

  if (!position)
  {
    /* Reset position and erase first sector */
    bridgeReset(loader);
    loader->erasingPosition = loader->currentPosition;
    loader->eraseQueued = true;
    wqAdd(WQ_DEFAULT, flashProgramTask, loader);
  }

  if (loader->currentPosition + length > loader->flashSize)
    return 0;

  size_t processed = 0;

  do
  {
    if (!length || loader->bufferSize == loader->chunkSize)
    {
      const enum Result res = ifSetParam(loader->flash, IF_POSITION,
          &loader->currentPosition);

      if (res != E_OK)
        return 0;

      const size_t written = ifWrite(loader->flash, loader->chunk,
          loader->chunkSize);

      if (written != loader->chunkSize)
        return 0;

      loader->currentPosition += loader->bufferSize;
      loader->bufferSize = 0;
      memset(loader->chunk, 0xFF, loader->chunkSize);

      if (isSectorAddress(loader, loader->currentPosition))
      {
        /* Enqueue sector erasure */
        loader->erasingPosition = loader->currentPosition;
        loader->eraseQueued = true;
        wqAdd(WQ_DEFAULT, flashProgramTask, loader);
      }
    }

    const size_t chunkSize = length <= loader->chunkSize - loader->bufferSize ?
        length : loader->chunkSize - loader->bufferSize;

    memcpy(loader->chunk + loader->bufferSize,
        (const uint8_t *)buffer + processed, chunkSize);

    loader->bufferSize += chunkSize;
    processed += chunkSize;
  }
  while (processed < length);

  *timeout = loader->eraseQueued ?
      getSectorEraseTime(loader, loader->erasingPosition) : 0;

  return length;
}
/*----------------------------------------------------------------------------*/
static size_t onUploadRequest(void *object, size_t position, void *buffer,
    size_t length)
{
  struct DfuBridge * const loader = object;
  const size_t offset = position + loader->flashOffset;

  if (offset + length > loader->flashSize)
    return 0;
  if (ifSetParam(loader->flash, IF_POSITION, &offset) != E_OK)
    return 0;

  return ifRead(loader->flash, buffer, length);
}
/*----------------------------------------------------------------------------*/
static enum Result bridgeInit(void *object, const void *configBase)
{
  const struct DfuBridgeConfig * const config = configBase;
  assert(config);

  struct DfuBridge * const loader = object;
  enum Result res;

  loader->flash = config->flash;
  loader->device = config->device;
  loader->reset = config->reset;
  loader->geometry = config->geometry;
  loader->flashOffset = config->offset;

  res = ifGetParam(loader->flash, IF_SIZE, &loader->flashSize);
  if (res != E_OK)
    return res;
  if (loader->flashOffset >= loader->flashSize)
    return E_VALUE;

  res = ifGetParam(loader->flash, IF_FLASH_PAGE_SIZE, &loader->chunkSize);
  if (res != E_OK)
    res = ifGetParam(loader->flash, IF_FLASH_SECTOR_SIZE, &loader->chunkSize);
  if (res != E_OK)
    res = ifGetParam(loader->flash, IF_FLASH_BLOCK_SIZE, &loader->chunkSize);
  if (res != E_OK)
    return res;

  loader->chunk = malloc(loader->chunkSize);
  if (!loader->chunk)
    return E_MEMORY;

  if (loader->reset)
    dfuSetDetachRequestCallback(loader->device, onDetachRequest);

  dfuSetCallbackArgument(loader->device, loader);
  dfuSetDownloadRequestCallback(loader->device, onDownloadRequest);

  if (!config->writeonly)
    dfuSetUploadRequestCallback(loader->device, onUploadRequest);

  bridgeReset(loader);
  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void bridgeDeinit(void *object)
{
  struct DfuBridge * const loader = object;

  dfuSetUploadRequestCallback(loader->device, 0);
  dfuSetDownloadRequestCallback(loader->device, 0);
  dfuSetDetachRequestCallback(loader->device, 0);
  dfuSetCallbackArgument(loader->device, 0);

  free(loader->chunk);
}
