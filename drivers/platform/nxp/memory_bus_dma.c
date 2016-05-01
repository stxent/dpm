/*
 * memory_bus_dma.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <memory.h>
#include <platform/nxp/gpdma.h>
#include <drivers/platform/nxp/memory_bus_dma.h>
#include <drivers/platform/nxp/memory_bus_dma_finalizer.h>
#include <drivers/platform/nxp/memory_bus_dma_timer.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
enum result setupDma(struct MemoryBusDma *, const struct MemoryBusDmaConfig *,
    uint8_t, uint8_t);
void setupGpio(struct MemoryBusDma *, const struct MemoryBusDmaConfig *);
/*----------------------------------------------------------------------------*/
static enum result busInit(void *, const void *);
static void busDeinit(void *);
static enum result busCallback(void *, void (*)(void *), void *);
static enum result busGet(void *, enum ifOption, void *);
static enum result busSet(void *, enum ifOption, const void *);
static size_t busRead(void *, void *, size_t);
static size_t busWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
static const struct InterfaceClass busTable = {
    .size = sizeof(struct MemoryBusDma),
    .init = busInit,
    .deinit = busDeinit,

    .callback = busCallback,
    .get = busGet,
    .set = busSet,
    .read = busRead,
    .write = busWrite
};
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const MemoryBusDma = &busTable;
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct MemoryBusDma * const interface = object;

  interface->active = false;

  if (interface->callback)
    interface->callback(interface->callbackArgument);
}
/*----------------------------------------------------------------------------*/
void setupGpio(struct MemoryBusDma *interface,
    const struct MemoryBusDmaConfig *config)
{
  assert(config->pins);

  union PinData firstPinKey;
  firstPinKey.key = ~config->pins[0];

  /* First pin number should be aligned along byte boundary */
  assert(!(firstPinKey.offset & 0x07));

  unsigned int number = 0;
  while (config->pins[number] && ++number < 32);

  /* Only byte and halfword are available */
  assert(number == 8 || number == 16);

  interface->width = (number >> 3) - 1;

  for (unsigned int index = 0; index < number; ++index)
  {
    const struct Pin pin = pinInit(config->pins[index]);
    assert(pinValid(pin));

    if (index)
    {
      assert(pin.data.port == firstPinKey.port);
      assert(pin.data.offset == firstPinKey.offset + index);
    }
    else
    {
      interface->gpioAddress = pinAddress(pin);
    }

    pinOutput(pin, 0);
  }
}
/*----------------------------------------------------------------------------*/
enum result setupDma(struct MemoryBusDma *interface,
    const struct MemoryBusDmaConfig *config, uint8_t matchChannel,
    uint8_t width)
{
  /* Only channels 0 and 1 can be used as DMA events */
  assert(matchChannel < 2);

  struct GpDmaConfig dmaConfig = {
      .channel = config->clock.dma,
      .destination.increment = false,
      .source.increment = true,
      .burst = DMA_BURST_1,
      .event = GPDMA_MAT0_0 + matchChannel + config->clock.channel * 2,
      .type = GPDMA_TYPE_M2P,
      .width = !width ? DMA_WIDTH_BYTE : DMA_WIDTH_HALFWORD
  };

  /*
   * To improve performance DMA synchronization logic can be disabled.
   * This will decrease data write time from 5 to 4 processor cycles.
   */

  interface->dma = init(GpDma, &dmaConfig);
  if (!interface->dma)
    return E_ERROR;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum result busInit(void *object, const void *configPtr)
{
  const struct MemoryBusDmaConfig * const config = configPtr;
  const struct MemoryBusDmaTimerConfig clockConfig = {
      .cycle = config->cycle,
      .input = 0,
      .leading = config->clock.leading,
      .trailing = config->clock.trailing,
      .priority = config->priority,
      .channel = config->clock.channel,
      .control = false,
      .inversion = config->clock.inversion
  };
  const struct MemoryBusDmaTimerConfig controlConfig = {
      .cycle = 0,
      .input = config->control.capture,
      .leading = config->control.leading,
      .trailing = config->control.trailing,
      .priority = config->priority,
      .channel = config->control.channel,
      .control = true,
      .inversion = config->control.inversion
  };
  struct MemoryBusDma * const interface = object;
  enum result res;

  setupGpio(interface, config);

  interface->control = init(MemoryBusDmaTimer, &controlConfig);
  if (!interface->control)
    return E_ERROR;

  timerSetEnabled(interface->control, false);

  interface->clock = init(MemoryBusDmaTimer, &clockConfig);
  if (!interface->clock)
    return E_ERROR;

  timerCallback(interface->clock, interruptHandler, interface);

  struct MemoryBusDmaFinalizerConfig finalizerConfig = {
      .marshal = interface->control,
      .sender = interface->clock,
      .channel = config->control.dma
  };

  interface->finalizer = init(MemoryBusDmaFinalizer, &finalizerConfig);
  if (!interface->finalizer)
    return E_ERROR;

  res = setupDma(interface, config,
      memoryBusDmaTimerPrimaryChannel(interface->clock), interface->width);
  if (res != E_OK)
    return res;

  interface->active = false;
  interface->blocking = true;
  interface->callback = 0;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void busDeinit(void *object)
{
  struct MemoryBusDma * const interface = object;

  deinit(interface->dma);
  deinit(interface->finalizer);
  deinit(interface->clock);
  deinit(interface->control);
}
/*----------------------------------------------------------------------------*/
static enum result busCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct MemoryBusDma * const interface = object;

  interface->callbackArgument = argument;
  interface->callback = callback;
  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum result busGet(void *object, enum ifOption option, void *data)
{
  struct MemoryBusDma * const interface = object;

  switch (option)
  {
    case IF_STATUS:
      return interface->active ? E_BUSY : E_OK;

    case IF_WIDTH:
      *(uint32_t *)data = 1 << (interface->width + 3);
      return E_OK;

    default:
      return E_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static enum result busSet(void *object, enum ifOption option,
    const void *data __attribute__((unused)))
{
  struct MemoryBusDma * const interface = object;

  switch (option)
  {
    case IF_BLOCKING:
      interface->blocking = true;
      return E_OK;

    case IF_ZEROCOPY:
      interface->blocking = false;
      return E_OK;

    default:
      return E_ERROR;
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
  const size_t samples = length >> interface->width;

  if (!samples)
    return 0;

  interface->active = true;

  /* Configure and start control timer */
  timerSetOverflow(interface->control, samples + 2);
  timerSetEnabled(interface->control, true);

  /* Finalization should be enabled after supervisor timer startup */
  if (memoryBusDmaFinalizerStart(interface->finalizer) != E_OK)
    return 0;

  if (dmaStart(interface->dma, interface->gpioAddress, buffer, samples) != E_OK)
  {
    memoryBusDmaFinalizerStop(interface->finalizer);
    timerSetEnabled(interface->control, false);
    return 0;
  }

  /* Start the transfer by enabling clock generation timer */
  timerSetEnabled(interface->clock, true);

  if (interface->blocking)
  {
    while (interface->active)
      barrier();

    if (dmaStatus(interface->dma) != E_OK)
    {
      /* Transmission has failed due to a noise on signal lines */
      dmaStop(interface->dma);
      return 0;
    }
  }

  return samples << interface->width;
}
