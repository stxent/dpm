/*
 * ws281x_ssp.c
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/ws281x_ssp.h>
#include <halm/platform/lpc/ssp_defs.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
#define DATA_BITRATE  2500000
#define IDLE_BITRATE  880000
#define FIFO_DEPTH    8

enum
{
  STATE_IDLE,
  STATE_DATA,
  STATE_RESET
};
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static uint32_t packSingleColor(uint8_t);
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *, const void *);
static void busDeinit(void *);
static void busSetCallback(void *, void (*)(void *), void *);
static enum Result busGetParam(void *, int, void *);
static enum Result busSetParam(void *, int, const void *);
static size_t busWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const WS281xSsp = &(const struct InterfaceClass){
    .size = sizeof(struct WS281xSsp),
    .init = busInit,
    .deinit = busDeinit,

    .setCallback = busSetCallback,
    .getParam = busGetParam,
    .setParam = busSetParam,
    .read = NULL,
    .write = busWrite
};
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct WS281xSsp * const interface = object;
  LPC_SSP_Type * const reg = interface->base.reg;

  /* Handle reception */
  size_t received = 0;

  while (reg->SR & SR_RNE)
  {
    (void)reg->DR;
    ++received;
  }

  interface->rxLeft -= received;

  /* Handle transmission */
  if (interface->txLeft)
  {
    const size_t space = FIFO_DEPTH - (interface->rxLeft - interface->txLeft);
    size_t pending = MIN(space, interface->txLeft);

    interface->txLeft -= pending;

    while (pending--)
      reg->DR = *interface->txPosition++;
  }

  /* Update the state when all frames have been received */
  if (!interface->rxLeft)
  {
    if (interface->state == STATE_DATA)
    {
      interface->state = STATE_RESET;
      interface->rxLeft = 1;

      /* Reconfigure bit rate */
      sspSetRate(&interface->base, IDLE_BITRATE);

      /* Enable RTIM interrupt only */
      reg->IMSC = IMSC_RTIM;
      __dsb();

      /*
       * Clear pending interrupt flag in NVIC. The ISR handler will be called
       * twice if the RTIM interrupt occurred after the RXIM interrupt
       * but before the RTIM interrupt was disabled.
       */
      irqClearPending(interface->base.irq);

      reg->DR = 0;
    }
    else if (interface->state == STATE_RESET)
    {
      interface->state = STATE_IDLE;

      /* Disable all interrupts */
      reg->IMSC = 0;

      if (interface->callback != NULL)
        interface->callback(interface->callbackArgument);
    }
  }
}
/*----------------------------------------------------------------------------*/
static uint32_t packSingleColor(uint8_t color)
{
  const uint32_t w0 = ((color >> 4) + (color << 16)) & 0x000F000FUL;
  const uint32_t w1 = (w0 | (w0 << 4)) & 0x00C300C3UL;
  const uint32_t w2 = (w1 | (w1 << 2)) & 0x02490249UL;

  return 0x09240924UL | (w2 << 1);
}
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *object, const void *configBase)
{
  const struct WS281xSspConfig * const config = configBase;
  assert(config != NULL);
  assert(config->size > 0);

  const struct SspBaseConfig baseConfig = {
      .cs = 0,
      .miso = 0,
      .mosi = config->mosi,
      .sck = 0,
      .channel = config->channel
  };
  struct WS281xSsp * const interface = object;
  enum Result res;

  /* Call base class constructor */
  if ((res = SspBase->init(interface, &baseConfig)) != E_OK)
    return res;

  interface->buffer = malloc(config->size * 3 * 2 * sizeof(uint16_t));
  if (interface->buffer == NULL)
    return E_MEMORY;

  interface->base.handler = interruptHandler;
  interface->callback = NULL;
  interface->blocking = true;
  interface->size = config->size;
  interface->state = STATE_IDLE;

  LPC_SSP_Type * const reg = interface->base.reg;

  /* Set frame size */
  reg->CR0 = CR0_DSS(12) | CR0_FRF(FRF_TI);

  /* Test bit rates */
  if (!sspSetRate(&interface->base, DATA_BITRATE))
    return E_VALUE;
  if (!sspSetRate(&interface->base, IDLE_BITRATE))
    return E_VALUE;

  /* Set SPI mode */
  sspSetMode(&interface->base, 0);

  /* Enable the peripheral */
  reg->CR1 = CR1_SSE;

  irqSetPriority(interface->base.irq, config->priority);
  irqEnable(interface->base.irq);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void busDeinit(void *object)
{
  struct WS281xSsp * const interface = object;
  LPC_SSP_Type * const reg = interface->base.reg;

  /* Disable the peripheral */
  irqDisable(interface->base.irq);
  reg->CR1 = 0;

  free(interface->buffer);
  SspBase->deinit(interface);
}
/*----------------------------------------------------------------------------*/
static void busSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct WS281xSsp * const interface = object;

  interface->callbackArgument = argument;
  interface->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result busGetParam(void *object, int parameter, void *)
{
  struct WS281xSsp * const interface = object;

  switch ((enum IfParameter)parameter)
  {
    case IF_STATUS:
      return interface->rxLeft > 0 ? E_BUSY : E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result busSetParam(void *object, int parameter, const void *)
{
  struct WS281xSsp * const interface = object;

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
static size_t busWrite(void *object, const void *buffer, size_t length)
{
  struct WS281xSsp * const interface = object;

  if (interface->state != STATE_IDLE)
    return 0;

  if (length > interface->size * 3)
    length = interface->size * 3;
  length -= length % 3;

  if (!length)
    return 0;

  const uint8_t *input = buffer;
  uint16_t *output = interface->buffer;

  for (size_t index = 0; index < length; index += 3)
  {
    const uint32_t rWord = packSingleColor(*input++);
    const uint32_t gWord = packSingleColor(*input++);
    const uint32_t bWord = packSingleColor(*input++);

    memcpy(&output[0], &gWord, sizeof(uint32_t));
    memcpy(&output[2], &rWord, sizeof(uint32_t));
    memcpy(&output[4], &bWord, sizeof(uint32_t));
    output += 6;
  }

  interface->txPosition = interface->buffer;
  interface->rxLeft = interface->txLeft = length * 2;
  interface->state = STATE_DATA;

  /* Clear interrupt flags and enable interrupts */
  LPC_SSP_Type * const reg = interface->base.reg;

  sspSetRate(&interface->base, DATA_BITRATE);
  reg->ICR = ICR_RORIC | ICR_RTIC;
  reg->IMSC = IMSC_RXIM | IMSC_RTIM;

  /* Initiate transmission by setting pending interrupt flag */
  irqSetPending(interface->base.irq);

  if (interface->blocking)
  {
    while (interface->state != STATE_IDLE)
      barrier();
  }

  return length;
}
