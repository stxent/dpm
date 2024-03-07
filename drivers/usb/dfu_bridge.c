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
enum
{
  OP_TYPE_UNDEFINED,
  OP_TYPE_PAGE,
  OP_TYPE_SECTOR,
  OP_TYPE_BLOCK
};
/*----------------------------------------------------------------------------*/
static void bridgeReset(struct DfuBridge *);
static void flashProgramTask(void *);
static uint32_t getSectorEraseTime(const struct DfuBridge *, size_t);
static bool isSectorAddress(const struct DfuBridge *, size_t);
static void onDetachRequest(void *, uint16_t);
static size_t onDownloadRequest(void *, size_t, const void *, size_t,
    uint16_t *);
static size_t onUploadRequest(void *, size_t, void *, size_t);
static inline enum FlashParameter opTypeToEraseParam(uint8_t);
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
static void bridgeReset(struct DfuBridge *loader)
{
  loader->bufferLevel = 0;
  loader->erasePosition = 0;
  loader->writePosition = loader->flashOffset;
  loader->eraseQueued = false;
  memset(loader->buffer, 0xFF, loader->writeChunkSize);
}
/*----------------------------------------------------------------------------*/
static inline enum FlashParameter opTypeToEraseParam(uint8_t type)
{
  assert(type != OP_TYPE_UNDEFINED);

  return type == OP_TYPE_PAGE ?
      IF_FLASH_ERASE_PAGE : (type == OP_TYPE_SECTOR ?
          IF_FLASH_ERASE_SECTOR : IF_FLASH_ERASE_BLOCK);
}
/*----------------------------------------------------------------------------*/
static void flashProgramTask(void *argument)
{
  struct DfuBridge * const loader = argument;

  loader->eraseQueued = false;

  const IrqState irqState = irqSave();
  ifSetParam(loader->flash, opTypeToEraseParam(loader->eraseType),
      &loader->erasePosition);
  dfuOnDownloadCompleted(loader->device, true);
  irqRestore(irqState);
}
/*----------------------------------------------------------------------------*/
static uint32_t getSectorEraseTime(const struct DfuBridge *loader,
    size_t address)
{
  const struct FlashGeometry * const region = flashFindRegion(loader->geometry,
      loader->regions, address);

  return region != NULL ? region->time : 0;
}
/*----------------------------------------------------------------------------*/
static bool isSectorAddress(const struct DfuBridge *loader, size_t address)
{
  const struct FlashGeometry * const region = flashFindRegion(loader->geometry,
      loader->regions, address);

  return region != NULL && (address & (region->size - 1)) == 0;
}
/*----------------------------------------------------------------------------*/
static void onDetachRequest(void *object, [[maybe_unused]] uint16_t timeout)
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
    loader->erasePosition = loader->writePosition;
    loader->eraseQueued = true;
    wqAdd(WQ_DEFAULT, flashProgramTask, loader);
  }

  if (loader->writePosition + length > loader->flashSize)
    return 0;

  size_t processed = 0;

  do
  {
    if (!length || loader->bufferLevel == loader->writeChunkSize)
    {
      const enum Result res = ifSetParam(loader->flash, IF_POSITION,
          &loader->writePosition);

      if (res != E_OK)
        return 0;

      const size_t written = ifWrite(loader->flash, loader->buffer,
          loader->writeChunkSize);

      if (written != loader->writeChunkSize)
        return 0;

      loader->writePosition += loader->bufferLevel;
      loader->bufferLevel = 0;
      memset(loader->buffer, 0xFF, loader->writeChunkSize);

      if (isSectorAddress(loader, loader->writePosition))
      {
        /* Enqueue sector erasure */
        loader->erasePosition = loader->writePosition;
        loader->eraseQueued = true;
        wqAdd(WQ_DEFAULT, flashProgramTask, loader);
      }
    }

    const size_t bytesLeft = loader->writeChunkSize - loader->bufferLevel;
    const size_t chunkSize = MIN(length, bytesLeft);

    memcpy(loader->buffer + loader->bufferLevel,
        (const uint8_t *)buffer + processed, chunkSize);

    loader->bufferLevel += chunkSize;
    processed += chunkSize;
  }
  while (processed < length);

  *timeout = loader->eraseQueued ?
      getSectorEraseTime(loader, loader->erasePosition) : 0;

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
  assert(config != NULL);
  assert(config->geometry != NULL && config->regions > 0);
  assert(config->device != NULL && config->flash != NULL);

  struct DfuBridge * const loader = object;
  enum Result res;

  loader->device = config->device;
  loader->reset = config->reset;

  loader->geometry = config->geometry;
  loader->regions = config->regions;

  loader->flash = config->flash;
  loader->flashOffset = config->offset;

  res = ifGetParam(loader->flash, IF_SIZE, &loader->flashSize);
  if (res != E_OK)
    return res;
  if (loader->flashOffset >= loader->flashSize)
    return E_VALUE;

  /* Sectors are used for flash devices with multiple regions */
  loader->eraseType = config->regions > 1 ? OP_TYPE_SECTOR : OP_TYPE_UNDEFINED;
  loader->writeChunkSize = 0;

  uint32_t size;

  res = ifGetParam(loader->flash, IF_FLASH_PAGE_SIZE, &size);
  if (res == E_OK)
  {
    if (config->regions == 1 && config->geometry[0].size == size)
      loader->eraseType = OP_TYPE_PAGE;
    if (!loader->writeChunkSize)
      loader->writeChunkSize = size;
  }

  res = ifGetParam(loader->flash, IF_FLASH_SECTOR_SIZE, &size);
  if (res == E_OK)
  {
    if (config->regions == 1 && config->geometry[0].size == size)
      loader->eraseType = OP_TYPE_SECTOR;
    if (!loader->writeChunkSize)
      loader->writeChunkSize = size;
  }

  res = ifGetParam(loader->flash, IF_FLASH_BLOCK_SIZE, &size);
  if (res == E_OK)
  {
    if (config->regions == 1 && config->geometry[0].size == size)
      loader->eraseType = OP_TYPE_BLOCK;
    if (!loader->writeChunkSize)
      loader->writeChunkSize = size;
  }

  if (loader->eraseType == OP_TYPE_UNDEFINED || !loader->writeChunkSize)
    return E_INTERFACE;

  loader->buffer = malloc(loader->writeChunkSize);
  if (loader->buffer == NULL)
    return E_MEMORY;

  if (loader->reset != NULL)
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

  dfuSetUploadRequestCallback(loader->device, NULL);
  dfuSetDownloadRequestCallback(loader->device, NULL);
  dfuSetDetachRequestCallback(loader->device, NULL);
  dfuSetCallbackArgument(loader->device, NULL);

  free(loader->buffer);
}
