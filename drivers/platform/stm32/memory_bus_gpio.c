/*
 * memory_bus_gpio.c
 * Copyright (C) 2019 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/stm32/memory_bus_gpio.h>
#include <dpm/platform/stm32/memory_bus_gpio_timer.h>
#include <xcore/memory.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
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
    if (!--interface->left)
      timerDisable(interface->timer);

    gpioBusWrite(interface->bus, (uint32_t)(*interface->buffer++));
  }
  else
  {
    interface->busy = false;

    if (interface->callback != NULL)
      interface->callback(interface->callbackArgument);
  }
}
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *object, const void *configPtr)
{
  const struct MemoryBusGpioConfig * const config = configPtr;
  assert(config != NULL);
  assert(config->bus != NULL);

  const struct MemoryBusGpioTimerConfig timerConfig = {
      .frequency = config->frequency,
      .cycle = config->cycle,
      .pin = config->strobe,
      .priority = config->priority,
      .channel = config->timer,
      .inversion = config->inversion
  };
  struct MemoryBusGpio *interface = object;

  interface->timer = init(MemoryBusGpioTimer, &timerConfig);
  if (interface->timer == NULL)
    return E_ERROR;
  timerSetCallback(interface->timer, interruptHandler, interface);

  interface->blocking = true;
  interface->busy = false;
  interface->bus = config->bus;
  interface->callback = NULL;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void busDeinit(void *object)
{
  struct MemoryBusGpio * const interface = object;
  deinit(interface->timer);
}
/*----------------------------------------------------------------------------*/
static void busSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct MemoryBusGpio * const interface = object;

  interface->callbackArgument = argument;
  interface->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result busGetParam(void *object, int parameter, void *)
{
  struct MemoryBusGpio * const interface = object;

  switch ((enum IfParameter)parameter)
  {
    case IF_STATUS:
      return interface->busy ? E_BUSY : E_OK;

    default:
      return E_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result busSetParam(void *object, int parameter, const void *)
{
  struct MemoryBusGpio * const interface = object;

  switch ((enum IfParameter)parameter)
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
static size_t busRead(void *, void *, size_t)
{
  /* Currently unsupported */
  return 0;
}
/*----------------------------------------------------------------------------*/
static size_t busWrite(void *object, const void *buffer, size_t length)
{
  if (length)
  {
    struct MemoryBusGpio * const interface = object;

    interface->busy = true;
    interface->buffer = buffer;
    interface->left = length;

    timerEnable(interface->timer);

    if (interface->blocking)
    {
      while (interface->busy)
        barrier();
    }
  }

  return length;
}
