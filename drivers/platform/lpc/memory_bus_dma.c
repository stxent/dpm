/*
 * memory_bus_dma.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/memory_bus_dma.h>
#include <dpm/platform/lpc/memory_bus_dma_finalizer.h>
#include <dpm/platform/lpc/memory_bus_dma_timer.h>
#include <halm/platform/lpc/gpdma_circular.h>
#include <halm/platform/lpc/gpdma_oneshot.h>
#include <xcore/memory.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static bool setupDma(struct MemoryBusDma *, const struct MemoryBusDmaConfig *,
    uint8_t, uint8_t);
static void setupGpio(struct MemoryBusDma *, const struct MemoryBusDmaConfig *);
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *, const void *);
static void busDeinit(void *);
static void busSetCallback(void *, void (*)(void *), void *);
static enum Result busGetParam(void *, int, void *);
static enum Result busSetParam(void *, int, const void *);
static size_t busRead(void *, void *, size_t);
static size_t busWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
static const struct InterfaceClass busTable = {
    .size = sizeof(struct MemoryBusDma),
    .init = busInit,
    .deinit = busDeinit,

    .setCallback = busSetCallback,
    .getParam = busGetParam,
    .setParam = busSetParam,
    .read = busRead,
    .write = busWrite
};
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const MemoryBusDma = &busTable;
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct MemoryBusDma * const interface = object;

  interface->busy = false;

  if (interface->callback)
    interface->callback(interface->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void setupGpio(struct MemoryBusDma *interface,
    const struct MemoryBusDmaConfig *config)
{
  assert(config->pins);

  /* First pin number should be aligned along byte boundary */
  assert(!(PIN_TO_OFFSET(config->pins[0]) & 0x07));

  size_t number = 0;
  while (config->pins[number] && ++number < 32);

  /* Only byte and halfword are available */
  assert(number == 8 || number == 16);
  interface->width = (number >> 3) - 1;

  for (size_t index = 0; index < number; ++index)
  {
    const struct Pin pin = pinInit(config->pins[index]);
    assert(pinValid(pin));

    if (index)
    {
      assert(pin.port == PIN_TO_PORT(config->pins[0]));
      assert(pin.number == PIN_TO_OFFSET(config->pins[0]) + index);
    }
    else
      interface->address = pinAddress(pin);

    pinOutput(pin, false);
  }
}
/*----------------------------------------------------------------------------*/
static bool setupDma(struct MemoryBusDma *interface,
    const struct MemoryBusDmaConfig *config, uint8_t matchChannel,
    uint8_t busWidth)
{
  /* Only channels 0 and 1 can be used as DMA events */
  assert(matchChannel < 2);

  const enum GpDmaWidth width = busWidth ? DMA_WIDTH_HALFWORD : DMA_WIDTH_BYTE;

  const struct GpDmaSettings dmaSettings = {
      .source = {
          .burst = DMA_BURST_4,
          .width = width,
          .increment = true
      },
      .destination = {
          .burst = DMA_BURST_1,
          .width = width,
          .increment = false
      }
  };

  /*
   * To improve performance DMA synchronization logic can be disabled.
   * This will decrease data write time from 5 to 4 AHB cycles.
   */

  if (config->size <= GPDMA_MAX_TRANSFER_SIZE)
  {
    const struct GpDmaOneShotConfig dmaConfig = {
        .event = GPDMA_MAT0_0 + matchChannel + config->clock.channel * 2,
        .type = GPDMA_TYPE_M2P,
        .channel = config->clock.dma
    };

    interface->dma = init(GpDmaOneShot, &dmaConfig);
  }
  else
  {
    const size_t chunks = (config->size + GPDMA_MAX_TRANSFER_SIZE - 1)
        / GPDMA_MAX_TRANSFER_SIZE;
    const struct GpDmaCircularConfig dmaConfig = {
        .number = chunks,
        .event = GPDMA_MAT0_0 + matchChannel + config->clock.channel * 2,
        .type = GPDMA_TYPE_M2P,
        .channel = config->clock.dma,
        .oneshot = true,
        .silent = true
    };

    interface->dma = init(GpDmaCircular, &dmaConfig);
  }

  if (interface->dma)
  {
    dmaConfigure(interface->dma, &dmaSettings);
    return true;
  }
  else
    return false;
}
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *object, const void *configPtr)
{
  const struct MemoryBusDmaConfig * const config = configPtr;
  const struct MemoryBusDmaClockConfig clockConfig = {
      .cycle = config->cycle,
      .leading = config->clock.leading,
      .trailing = config->clock.trailing,
      .priority = config->priority,
      .channel = config->clock.channel,
      .inversion = config->clock.inversion
  };
  const struct MemoryBusDmaControlConfig controlConfig = {
      .input = config->control.capture,
      .leading = config->control.leading,
      .trailing = config->control.trailing,
      .channel = config->control.channel,
      .inversion = config->control.inversion
  };
  struct MemoryBusDma * const interface = object;

  setupGpio(interface, config);

  interface->clock = init(MemoryBusDmaClock, &clockConfig);
  if (!interface->clock)
    return E_ERROR;

  interface->control = init(MemoryBusDmaControl, &controlConfig);
  if (!interface->control)
    return E_ERROR;

  struct MemoryBusDmaFinalizerConfig finalizerConfig = {
      .marshal = interface->control,
      .sender = interface->clock,
      .channel = config->control.dma
  };

  interface->finalizer = init(MemoryBusDmaFinalizer, &finalizerConfig);
  if (!interface->finalizer)
    return E_ERROR;

  const uint8_t dmaEvent = config->clock.swap ?
      interface->clock->trailing : interface->clock->leading;

  if (setupDma(interface, config, dmaEvent, interface->width))
  {
    timerSetCallback(interface->clock, interruptHandler, interface);

    interface->blocking = true;
    interface->busy = false;
    interface->callback = 0;

    return E_OK;
  }
  else
    return E_ERROR;
}
/*----------------------------------------------------------------------------*/
static void busDeinit(void *object)
{
  struct MemoryBusDma * const interface = object;

  deinit(interface->dma);
  deinit(interface->finalizer);
  deinit(interface->control);
  deinit(interface->clock);
}
/*----------------------------------------------------------------------------*/
static void busSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct MemoryBusDma * const interface = object;

  interface->callbackArgument = argument;
  interface->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result busGetParam(void *object, int parameter, void *data)
{
  struct MemoryBusDma * const interface = object;

  switch ((enum IfParameter)parameter)
  {
    case IF_STATUS:
      return interface->busy ? E_BUSY : E_OK;

    case IF_WIDTH:
      *(size_t *)data = 1 << (interface->width + 3);
      return E_OK;

    default:
      return E_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result busSetParam(void *object, int parameter,
    const void *data __attribute__((unused)))
{
  struct MemoryBusDma * const interface = object;

  switch ((enum IfParameter)parameter)
  {
    case IF_BLOCKING:
      interface->blocking = true;
      return E_OK;

    case IF_ZEROCOPY:
      interface->blocking = false;
      return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static size_t busRead(void *object __attribute__((unused)),
    void *buffer __attribute__((unused)),
    size_t length __attribute__((unused)))
{
  return 0;
}
/*----------------------------------------------------------------------------*/
static size_t busWrite(void *object, const void *buffer, size_t length)
{
  struct MemoryBusDma * const interface = object;
  size_t samples = length >> interface->width;

  if (!samples)
    return 0;

  interface->busy = true;

  /* Configure and start control timer */
  timerSetOverflow(interface->control, samples + 1);
  timerEnable(interface->control);

  /* Finalization should be enabled after supervisor timer startup */
  if (memoryBusDmaFinalizerStart(interface->finalizer) != E_OK)
  {
    timerDisable(interface->control);
    return 0;
  }

  uintptr_t position = (uintptr_t)buffer;

  while (samples)
  {
    const size_t chunk = MIN(samples, GPDMA_MAX_TRANSFER_SIZE);

    dmaAppend(interface->dma, interface->address, (const void *)position,
        chunk);

    samples -= chunk;
    position += chunk << interface->width;
  }

  if (dmaEnable(interface->dma) != E_OK)
    goto error;

  /* Start the transfer by enabling the clock generation timer */
  timerEnable(interface->clock);

  if (interface->blocking)
  {
    while (interface->busy)
      barrier();

    if (dmaStatus(interface->dma) != E_OK)
    {
      timerDisable(interface->clock);
      goto error;
    }
  }

  return length - (samples << interface->width);

error:
  /* Transmission has failed */
  memoryBusDmaFinalizerStop(interface->finalizer);
  timerDisable(interface->control);
  return 0;
}
