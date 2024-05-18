/*
 * sgpio_bus.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/sgpio_bus.h>
#include <dpm/platform/lpc/sgpio_bus_dma.h>
#include <dpm/platform/lpc/sgpio_bus_timer.h>
#include <halm/platform/lpc/lpc43xx/gima_defs.h>
#include <halm/platform/lpc/sgpio_base.h>
#include <halm/platform/lpc/sgpio_defs.h>
#include <xcore/accel.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
static bool enqueueNextTransfer(struct SgpioBus *);
static void interruptHandler(void *);
static bool setupDma(struct SgpioBus *, uint8_t, uint8_t, uint8_t);
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *, const void *);
static void busDeinit(void *);
static void busSetCallback(void *, void (*)(void *), void *);
static enum Result busGetParam(void *, int, void *);
static enum Result busSetParam(void *, int, const void *);
static size_t busWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const SgpioBus =
    &(const struct InterfaceClass){
    .size = sizeof(struct SgpioBus),
    .init = busInit,
    .deinit = busDeinit,

    .setCallback = busSetCallback,
    .getParam = busGetParam,
    .setParam = busSetParam,
    .read = NULL,
    .write = busWrite
};
/*----------------------------------------------------------------------------*/
static bool enqueueNextTransfer(struct SgpioBus *interface)
{
  /* Maximum chunk size is 255 * 16 words or 16320 bytes */
  uint32_t chunk = MIN(interface->length, 16320);
  const int chunkPow = 32 - countLeadingZeros32((uint32_t)chunk);
  const int prescalerPow = MAX(chunkPow - 8, 0);
  const uint32_t chunkMask = MASK(chunkPow) ^ MASK(prescalerPow);
  uint32_t word = 0;

  chunk &= chunkMask;
  assert(chunk > 0);

  if (chunk >= sizeof(uint32_t))
  {
    /* Transfer involves DMA operations, align size */
    chunk &= ~(sizeof(uint32_t) - 1);

    /* Buffer address is aligned */
    word = *(const uint32_t *)interface->buffer;
  }
  else
  {
    switch (chunk)
    {
      case 3:
        word = interface->buffer[2] << 16;
        /* Falls through */
      case 2:
        word |= interface->buffer[1] << 8;
        /* Falls through */
      case 1:
        word |= interface->buffer[0];
        break;
    }
  }

  LPC_SGPIO_Type * const reg = interface->base.reg;
  uint32_t qualifierDiv;
  uint32_t qualifierPos;

  if (chunk > sizeof(uint32_t))
  {
    qualifierDiv = ((interface->prescaler * 4) << prescalerPow) - 1;
    qualifierPos = chunk >> prescalerPow;

    /* Enable the timer and clear pending DMA requests */
    timerEnable(interface->timer);

    /* Remaining data will be transmitted over DMA */
    dmaAppend(interface->dma, (void *)&reg->REG_SS[interface->slices.chain],
        (const void *)(interface->buffer + sizeof(uint32_t)),
        chunk - sizeof(uint32_t));
    if (dmaEnable(interface->dma) != E_OK)
      return false;
  }
  else
  {
    qualifierDiv = interface->prescaler * 4 - 1;
    qualifierPos = chunk;
  }

  interface->buffer += chunk;
  interface->length -= chunk;

  /* Disable all slices, clear asynchronous disable flag */
  reg->CTRL_ENABLE = 0;
  reg->CTRL_DISABLE = 0;

  reg->PRESET[interface->slices.qualifier] = qualifierDiv;
  reg->COUNT[interface->slices.qualifier] = 0;
  reg->POS[interface->slices.qualifier] = POS_POS(qualifierPos);
  reg->REG[interface->slices.qualifier] = 0xFFFFFFFFUL;
  reg->REG_SS[interface->slices.qualifier] = 0;

  /* Shift internal data clock by 4 cycles */
  reg->COUNT[interface->slices.gate] = interface->prescaler * 4 - 1;
  reg->POS[interface->slices.gate] = POS_POS(31) | POS_POS_RESET(31);
  reg->REG[interface->slices.gate] = 0;

  /* Shift output clock phase by 1 cycle */
  reg->COUNT[interface->slices.clock] = interface->prescaler - 1;
  reg->POS[interface->slices.clock] = POS_POS(31) | POS_POS_RESET(31);
  reg->REG[interface->slices.clock] = interface->inversion ?
      0x99999999UL : 0x66666666UL;

  /* Shift output clock phase by 1 cycle */
  reg->COUNT[interface->slices.dma] = interface->prescaler - 1;
  reg->POS[interface->slices.dma] = POS_POS(31) | POS_POS_RESET(31);
  reg->REG[interface->slices.dma] = 0x66666666UL;

  reg->COUNT[interface->slices.chain] = 0;
  reg->POS[interface->slices.chain] = POS_POS(3) | POS_POS_RESET(3);
  reg->REG[interface->slices.chain] = word;
  reg->REG_SS[interface->slices.chain] = 0;

  /* Enable slices */
  const IrqState state = irqSave();
  reg->CTRL_ENABLE = interface->controlEnableMask;
  reg->CTRL_DISABLE = interface->controlDisableMask;
  irqRestore(state);

  return true;
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct SgpioBus * const interface = object;
  LPC_SGPIO_Type * const reg = interface->base.reg;
  bool event = false;

  /* Clear pending exchange interrupt flag */
  reg->CLR_STATUS_1 = 1 << interface->slices.qualifier;
  /* Stop DMA request generation */
  timerDisable(interface->timer);

  if (interface->length)
  {
    if (!enqueueNextTransfer(interface))
      event = true;
  }
  else
    event = true;

  if (event)
  {
    interface->busy = false;

    if (interface->callback != NULL)
      interface->callback(interface->callbackArgument);
  }
}
/*----------------------------------------------------------------------------*/
static bool setupDma(struct SgpioBus *interface, uint8_t dmaChannel,
    uint8_t timerChannel, uint8_t matchChannel)
{
  /* Only channels 0 and 1 can be used as DMA events */
  assert(matchChannel < 2);

  const struct GpDmaSettings dmaSettings = {
      .source = {
          .burst = DMA_BURST_4,
          .width = DMA_WIDTH_WORD,
          .increment = true
      },
      .destination = {
          .burst = DMA_BURST_1,
          .width = DMA_WIDTH_WORD,
          .increment = false
      }
  };

  const struct SgpioBusDmaConfig dmaConfig = {
      .event = GPDMA_MAT0_0 + timerChannel * 2 + matchChannel,
      .dstMaster = GPDMA_MASTER_0,
      .srcMaster = GPDMA_MASTER_0,
      .type = GPDMA_TYPE_M2P,
      .channel = dmaChannel
  };

  interface->dma = init(SgpioBusDma, &dmaConfig);

  if (interface->dma != NULL)
  {
    dmaConfigure(interface->dma, &dmaSettings);
    return true;
  }
  else
    return false;
}
/*----------------------------------------------------------------------------*/
static enum Result busInit(void *object, const void *configBase)
{
  const struct SgpioBusConfig * const config = configBase;
  assert(config != NULL);
  assert(config->prescaler <= (1 << 5));

  struct SgpioBus * const interface = object;
  enum Result res;

  /* Call base class constructor */
  if ((res = SgpioBase->init(interface, NULL)) != E_OK)
    return res;

  const enum SgpioPin pinClock = sgpioConfigPin(config->pins.clock, PIN_NOPULL);
  const enum SgpioPin pinTimer = config->pins.dma;

  if (pinTimer != SGPIO_3 && pinTimer != SGPIO_12)
    return E_VALUE;

  const struct SgpioBusTimerConfig timerConfig = {
      .channel = pinTimer == SGPIO_3 ? 0 : 1,
      .capture = 0,
      .match = 0
  };

  interface->timer = init(SgpioBusTimer, &timerConfig);
  if (interface->timer == NULL)
    return E_ERROR;
  if (!setupDma(interface, config->dma, timerConfig.channel, 0))
    return E_ERROR;

  if (pinTimer == SGPIO_3)
  {
    LPC_GIMA->CAP0_0_IN = GIMA_SYNCH | GIMA_SELECT(1);
  }
  else
  {
    LPC_GIMA->CAP1_0_IN = GIMA_SYNCH | GIMA_SELECT(1);
  }

  const enum SgpioPin pinOutput[8] = {
      sgpioConfigPin(config->pins.data[0], PIN_NOPULL),
      sgpioConfigPin(config->pins.data[1], PIN_NOPULL),
      sgpioConfigPin(config->pins.data[2], PIN_NOPULL),
      sgpioConfigPin(config->pins.data[3], PIN_NOPULL),
      sgpioConfigPin(config->pins.data[4], PIN_NOPULL),
      sgpioConfigPin(config->pins.data[5], PIN_NOPULL),
      sgpioConfigPin(config->pins.data[6], PIN_NOPULL),
      sgpioConfigPin(config->pins.data[7], PIN_NOPULL)
  };

  interface->slices.clock = sgpioPinToSlice(pinClock, OUT_DOUTM1);
  interface->slices.dma = sgpioPinToSlice(pinTimer, OUT_DOUTM1);
  interface->slices.gate = config->slices.gate;
  interface->slices.qualifier = config->slices.qualifier;
  interface->controlEnableMask = 1 << interface->slices.qualifier
      | 1 << interface->slices.clock
      | 1 << interface->slices.dma
      | 1 << interface->slices.gate
      | 1 << interface->slices.chain;
  interface->controlDisableMask = 1 << interface->slices.qualifier;

  interface->base.handler = interruptHandler;
  interface->callback = NULL;

  interface->buffer = 0;
  interface->length = 0;
  interface->blocking = true;
  interface->busy = false;

  interface->prescaler = config->prescaler;
  interface->inversion = config->inversion;

  const enum SgpioSlice firstDataSlice = sgpioPinToSlice(pinOutput[0],
      OUT_DOUTM8A);

  if (firstDataSlice == SGPIO_SLICE_A)
  {
    interface->slices.chain = SGPIO_SLICE_A;
  }
  else
  {
    interface->slices.chain = SGPIO_SLICE_B;
  }

  const int8_t gateClockSource =
      sgpioSliceToClockSource(interface->slices.gate);

  if (gateClockSource == -1)
    return E_VALUE;

  LPC_SGPIO_Type * const reg = interface->base.reg;
  uint32_t oenreg = 0;

  /* Configure IO */
  reg->OUT_MUX_CFG[0] = OUT_MUX_CFG_P_OUT_CFG(OUT_DOUTM1)
      | OUT_MUX_CFG_P_OE_CFG(OE_GPIO);

  reg->OUT_MUX_CFG[pinClock] = OUT_MUX_CFG_P_OUT_CFG(OUT_DOUTM1)
      | OUT_MUX_CFG_P_OE_CFG(OE_GPIO);
  reg->OUT_MUX_CFG[pinTimer] = OUT_MUX_CFG_P_OUT_CFG(OUT_DOUTM1)
      | OUT_MUX_CFG_P_OE_CFG(OE_GPIO);

  for (size_t i = 0; i < ARRAY_SIZE(pinOutput); ++i)
  {
    reg->OUT_MUX_CFG[pinOutput[i]] = OUT_MUX_CFG_P_OUT_CFG(OUT_DOUTM8A)
        | OUT_MUX_CFG_P_OE_CFG(OE_GPIO);
  }

  oenreg |= 1 << (uint8_t)pinClock;
  for (size_t i = 0; i < ARRAY_SIZE(pinOutput); ++i)
    oenreg |= 1 << (uint8_t)pinOutput[i];

  reg->GPIO_OENREG = oenreg;

  /* Configure qualifier slice */
  reg->SGPIO_MUX_CFG[interface->slices.qualifier] = SGPIO_MUX_CFG_INT_CLK_ENABLE
      | SGPIO_MUX_CFG_QUALIFIER_MODE(QUALIFIER_ENABLE)
      | SGPIO_MUX_CFG_CONCAT_ENABLE
      | SGPIO_MUX_CFG_CONCAT_ORDER(CONCAT_SELF_LOOP);
  reg->SLICE_MUX_CFG[interface->slices.qualifier] =
      SLICE_MUX_CFG_CLK_CAPTURE_MODE(CLK_CAP_RISING)
      | SLICE_MUX_CFG_CLKGEN_MODE(CLK_GEN_INTERNAL);
  reg->SET_EN_1 = 1 << interface->slices.qualifier;

  /* Configure gate slice for data slices */
  reg->SGPIO_MUX_CFG[interface->slices.gate] = SGPIO_MUX_CFG_INT_CLK_ENABLE
      | SGPIO_MUX_CFG_QUALIFIER_MODE(QUALIFIER_SLICE)
      | SGPIO_MUX_CFG_QUALIFIER_SLICE_MODE(sgpioSliceToQualifierSource(
          interface->slices.qualifier, interface->slices.gate))
      | SGPIO_MUX_CFG_CONCAT_ENABLE
      | SGPIO_MUX_CFG_CONCAT_ORDER(CONCAT_SELF_LOOP);
  reg->SLICE_MUX_CFG[interface->slices.gate] = SLICE_MUX_CFG_MATCH_MODE
      | SLICE_MUX_CFG_CLK_CAPTURE_MODE(CLK_CAP_RISING)
      | SLICE_MUX_CFG_CLKGEN_MODE(CLK_GEN_INTERNAL);
  reg->PRESET[interface->slices.gate] = interface->prescaler * 4 - 1;

  /* Configure clock output slice */
  reg->SGPIO_MUX_CFG[interface->slices.clock] = SGPIO_MUX_CFG_INT_CLK_ENABLE
      | SGPIO_MUX_CFG_QUALIFIER_MODE(QUALIFIER_SLICE)
      | SGPIO_MUX_CFG_QUALIFIER_SLICE_MODE(sgpioSliceToQualifierSource(
          interface->slices.qualifier, interface->slices.clock))
      | SGPIO_MUX_CFG_CONCAT_ENABLE
      | SGPIO_MUX_CFG_CONCAT_ORDER(CONCAT_SELF_LOOP);
  reg->SLICE_MUX_CFG[interface->slices.clock] = SLICE_MUX_CFG_MATCH_MODE
      | SLICE_MUX_CFG_CLK_CAPTURE_MODE(CLK_CAP_RISING)
      | SLICE_MUX_CFG_CLKGEN_MODE(CLK_GEN_INTERNAL);
  reg->PRESET[interface->slices.clock] = interface->prescaler - 1;

  /* Configure DMA event slice */
  reg->SGPIO_MUX_CFG[interface->slices.dma] = SGPIO_MUX_CFG_INT_CLK_ENABLE
      | SGPIO_MUX_CFG_QUALIFIER_MODE(QUALIFIER_SLICE)
      | SGPIO_MUX_CFG_QUALIFIER_SLICE_MODE(sgpioSliceToQualifierSource(
          interface->slices.qualifier, interface->slices.dma))
      | SGPIO_MUX_CFG_CONCAT_ENABLE
      | SGPIO_MUX_CFG_CONCAT_ORDER(CONCAT_SELF_LOOP);
  reg->SLICE_MUX_CFG[interface->slices.dma] = SLICE_MUX_CFG_MATCH_MODE
      | SLICE_MUX_CFG_CLK_CAPTURE_MODE(CLK_CAP_RISING)
      | SLICE_MUX_CFG_CLKGEN_MODE(CLK_GEN_INTERNAL);
  reg->PRESET[interface->slices.dma] = interface->prescaler - 1;

  /* Configure first data slice */
  reg->SGPIO_MUX_CFG[interface->slices.chain] = SGPIO_MUX_CFG_INT_CLK_ENABLE
      | SGPIO_MUX_CFG_CLK_SOURCE_SLICE_MODE(gateClockSource)
      | SGPIO_MUX_CFG_QUALIFIER_MODE(QUALIFIER_SLICE)
      | SGPIO_MUX_CFG_QUALIFIER_SLICE_MODE(sgpioSliceToQualifierSource(
          interface->slices.qualifier, interface->slices.chain))
      | SGPIO_MUX_CFG_CONCAT_ENABLE
      | SGPIO_MUX_CFG_CONCAT_ORDER(CONCAT_SELF_LOOP);
  reg->SLICE_MUX_CFG[interface->slices.chain] =
      SLICE_MUX_CFG_CLK_CAPTURE_MODE(CLK_CAP_RISING)
      | SLICE_MUX_CFG_CLKGEN_MODE(CLK_GEN_EXTERNAL)
      | SLICE_MUX_CFG_PARALLEL_MODE(PARALLEL_MODE_8_BIT);
  reg->PRESET[interface->slices.chain] = 0;

  timerSetOverflow(interface->timer, 8);
  irqSetPriority(interface->base.irq, config->priority);
  irqEnable(interface->base.irq);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void busDeinit(void *object)
{
  struct SgpioBus * const interface = object;
  LPC_SGPIO_Type * const reg = interface->base.reg;

  irqDisable(interface->base.irq);
  reg->CTRL_ENABLE = 0;

  deinit(interface->dma);
  deinit(interface->timer);

  SgpioBase->deinit(interface);
}
/*----------------------------------------------------------------------------*/
static void busSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct SgpioBus * const interface = object;

  interface->callbackArgument = argument;
  interface->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result busGetParam(void *object, int parameter, void *)
{
  struct SgpioBus * const interface = object;

  switch ((enum IfParameter)parameter)
  {
    case IF_STATUS:
      return interface->busy ? E_BUSY : E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result busSetParam(void *object, int parameter, const void *)
{
  struct SgpioBus * const interface = object;

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
  struct SgpioBus * const interface = object;

  if (length)
  {
    interface->busy = true;
    interface->buffer = buffer;
    interface->length = length;

    if (!enqueueNextTransfer(interface))
      return 0;

    if (interface->blocking)
    {
      while (interface->busy)
        barrier();
    }
  }

  return length;
}
