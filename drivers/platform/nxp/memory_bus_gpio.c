/*
 * memory_bus_gpio.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <xcore/memory.h>
#include <dpm/drivers/platform/nxp/memory_bus_gpio.h>
#include <dpm/drivers/platform/nxp/memory_bus_gpio_timer.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *, const void *);
static void busDeinit(void *);
static enum Result busSetCallback(void *, void (*)(void *), void *);
static enum Result busGetParam(void *, enum IfParameter, void *);
static enum Result busSetParam(void *, enum IfParameter, const void *);
static size_t busRead(void *, void *, size_t);
static size_t busWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
static const struct InterfaceClass busTable = {
    .size = sizeof(struct MemoryBusGpio),
    .init = busInit,
    .deinit = busDeinit,

    .setCallback = busSetCallback,
    .getParam = busGetParam,
    .setParam = busSetParam,
    .read = busRead,
    .write = busWrite
};
/*----------------------------------------------------------------------------*/
const struct InterfaceClass *MemoryBusGpio = &busTable;
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct MemoryBusGpio *interface = object;

  if (interface->left)
  {
    if (--interface->left)
    {
      gpioBusWrite(interface->bus, (uint32_t)(*interface->buffer));
      ++interface->buffer;
    }
    else
      timerDisable(interface->timer);
  }
  else
  {
    interface->active = false;

    if (interface->callback)
      interface->callback(interface->callbackArgument);
  }
}
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *object, const void *configPtr)
{
  const struct MemoryBusGpioConfig * const config = configPtr;
  const struct MemoryBusGpioTimerConfig timerConfig = {
      .frequency = config->frequency,
      .cycle = config->cycle,
      .pin = config->strobe,
      .priority = config->priority,
      .channel = config->timer,
      .inversion = config->inversion
  };
  struct MemoryBusGpio *interface = object;

  assert(config->bus);

  interface->timer = init(MemoryBusGpioTimer, &timerConfig);
  if (!interface->timer)
    return E_ERROR;
  timerSetCallback(interface->timer, interruptHandler, interface);

  interface->active = false;
  interface->blocking = true;
  interface->bus = config->bus;
  interface->callback = 0;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void busDeinit(void *object)
{
  struct MemoryBusGpio * const interface = object;

  deinit(interface->timer);
}
/*----------------------------------------------------------------------------*/
static enum Result busSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct MemoryBusGpio * const interface = object;

  interface->callbackArgument = argument;
  interface->callback = callback;
  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum Result busGetParam(void *object, enum IfParameter parameter,
    void *data __attribute__((unused)))
{
  struct MemoryBusGpio * const interface = object;

  switch (parameter)
  {
    case IF_STATUS:
      return interface->active ? E_BUSY : E_OK;

    default:
      return E_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result busSetParam(void *object, enum IfParameter parameter,
    const void *data __attribute__((unused)))
{
  struct MemoryBusGpio * const interface = object;

  switch (parameter)
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
  /* Currently unsupported */
  return 0;
}
/*----------------------------------------------------------------------------*/
static size_t busWrite(void *object, const void *buffer, size_t length)
{
  struct MemoryBusGpio * const interface = object;

  interface->active = true;
  interface->buffer = buffer;
  interface->left = length;

  gpioBusWrite(interface->bus, (uint32_t)(*interface->buffer++));
  timerEnable(interface->timer);

  if (interface->blocking)
  {
    while (interface->active)
      barrier();
  }

  return length;
}
