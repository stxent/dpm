/*
 * irda.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <dpm/drivers/platform/stm32/irda.h>
#include <halm/platform/stm32/gptimer.h>
#include <halm/platform/stm32/uart_defs.h>
/*----------------------------------------------------------------------------*/
#define FRAME_WIDTH 10
/*----------------------------------------------------------------------------*/
enum State
{
  STATE_IDLE,
  STATE_WAIT,
  STATE_BREAK,
  STATE_DATA
};
/*----------------------------------------------------------------------------*/
static uint32_t calcTimerPeriod(size_t, bool);
static void serialInterruptHandler(void *);
static void timerInterruptHandler(void *);
/*----------------------------------------------------------------------------*/
static enum Result serialInit(void *, const void *);
static void serialSetCallback(void *, void (*)(void *), void *);
static enum Result serialGetParam(void *, int, void *);
static enum Result serialSetParam(void *, int, const void *);
static size_t serialRead(void *, void *, size_t);
static size_t serialWrite(void *, const void *, size_t);

#ifndef CONFIG_PLATFORM_STM32_UART_NO_DEINIT
static void serialDeinit(void *);
#else
#define serialDeinit deletedDestructorTrap
#endif
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const Irda = &(const struct InterfaceClass){
    .size = sizeof(struct Irda),
    .init = serialInit,
    .deinit = serialDeinit,

    .setCallback = serialSetCallback,
    .getParam = serialGetParam,
    .setParam = serialSetParam,
    .read = serialRead,
    .write = serialWrite
};
/*----------------------------------------------------------------------------*/
static uint32_t calcTimerPeriod(size_t width, bool master)
{
  return FRAME_WIDTH * (master ? ((width + 2) * 2) : (width + 1));
}
/*----------------------------------------------------------------------------*/
static void serialInterruptHandler(void *object)
{
  struct Irda * const interface = object;
  STM_USART_Type * const reg = interface->base.base.reg;
  uint32_t control = reg->CR1;
  uint32_t status = reg->SR;
  bool event = false;

  /* Handle reception timeout */
  if (status & SR_IDLE)
  {
    /* Software sequence to clear IDLE flag */
    (void)reg->DR;

    if (!byteQueueEmpty(&interface->base.rxQueue))
      event = true;
  }

  if (interface->state == STATE_BREAK)
  {
    control &= ~CR1_TCIE;

    /*
     * Invoke user callback at the start of the frame when there is
     * an unread data in the reception buffer.
     */
    if (!byteQueueEmpty(&interface->base.rxQueue))
    {
      event = true;
    }

    if (!byteQueueEmpty(&interface->base.txQueue))
    {
      const size_t txQueueSize = byteQueueSize(&interface->base.txQueue);

      control |= CR1_TXEIE;
      interface->pending = MIN(txQueueSize, interface->width);
      interface->state = STATE_DATA;
    }
    else
      interface->state = STATE_IDLE;
  }

  if (status & SR_LBD)
  {
    /* Handle Break interrupt */
    reg->SR = status & ~SR_LBD;

    if (!interface->master)
    {
      /* Restart the timer */
      timerDisable(interface->timer);
      timerSetValue(interface->timer, 0);
      timerEnable(interface->timer);

      interface->state = STATE_WAIT;
    }
  }
  else if (status & SR_RXNE)
  {
    /* Handle received data */
    const uint8_t data = reg->DR;

    if (!(status & SR_FE))
    {
      if (!byteQueueFull(&interface->base.rxQueue))
        byteQueuePushBack(&interface->base.rxQueue, data);
    }
  }

  /* Send remaining data */
  if ((control & CR1_TXEIE) && (status & SR_TXE))
  {
    reg->DR = byteQueuePopFront(&interface->base.txQueue);

    if (!--interface->pending)
    {
      control &= ~CR1_TXEIE;
      interface->state = STATE_IDLE;
    }
  }

  reg->CR1 = control;

  if (interface->base.callback && event)
    interface->base.callback(interface->base.callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void timerInterruptHandler(void *object)
{
  struct Irda * const interface = object;

  if (interface->master || !byteQueueEmpty(&interface->base.txQueue))
  {
    STM_USART_Type * const reg = interface->base.base.reg;

    /* Generate Break character */
    reg->SR &= ~SR_TC;
    reg->CR1 |= CR1_SBK | CR1_TCIE;

    interface->state = STATE_BREAK;
  }
  else if (!byteQueueEmpty(&interface->base.rxQueue))
  {
    interface->state = STATE_BREAK;
    irqSetPending(interface->base.base.irq);
  }

  if (!interface->master)
    timerDisable(interface->timer);
}
/*----------------------------------------------------------------------------*/
static enum Result serialInit(void *object, const void *configBase)
{
  const struct IrdaConfig * const config = configBase;
  assert(config);
  assert(config->frameLength <= config->rxLength);

  const struct SerialConfig baseConfig = {
      .rate = config->rate,
      .rxLength = config->rxLength,
      .txLength = config->txLength,
      .parity = SERIAL_PARITY_NONE,
      .rx = config->rx,
      .tx = config->tx,
      .priority = config->priority,
      .channel = config->channel
  };
  struct Irda * const interface = object;
  enum Result res;

  /* Call base class constructor */
  if ((res = Serial->init(object, &baseConfig)) != E_OK)
    return res;

  const struct GpTimerConfig timerConfig = {
      .frequency = config->rate,
      .priority = config->priority,
      .channel = config->timer,
      .event = 0
  };
  if (!(interface->timer = init(GpTimer, &timerConfig)))
    return E_ERROR;

  interface->base.base.handler = serialInterruptHandler;
  interface->pending = 0;
  interface->width = config->frameLength;
  interface->master = config->master;
  interface->state = STATE_IDLE;

  timerSetCallback(interface->timer, timerInterruptHandler, interface);
  timerSetOverflow(interface->timer, calcTimerPeriod(interface->width,
      interface->master));

  STM_USART_Type * const reg = interface->base.base.reg;

  /* Disable the peripheral before reconfiguration */
  reg->CR1 &= ~CR1_UE;

  /* Enable Break interrupt */
  reg->CR2 |= CR2_LBDIE;
  /* Enable IrDA */
  reg->CR3 |= CR3_IREN;
  /* In normal IrDA mode prescaler must be set to 1 */
  reg->GTPR = GTPR_PSC(1);

  /* Enable the peripheral */
  reg->CR1 |= CR1_UE;

  if (interface->master)
    timerEnable(interface->timer);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
#ifndef CONFIG_PLATFORM_STM32_UART_NO_DEINIT
static void serialDeinit(void *object)
{
  struct Irda * const interface = object;

  deinit(interface->timer);
  Serial->deinit(interface);
}
#endif
/*----------------------------------------------------------------------------*/
static void serialSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  Serial->setCallback(object, callback, argument);
}
/*----------------------------------------------------------------------------*/
static enum Result serialGetParam(void *object, int parameter, void *data)
{
  return Serial->getParam(object, parameter, data);
}
/*----------------------------------------------------------------------------*/
static enum Result serialSetParam(void *object, int parameter, const void *data)
{
  return Serial->setParam(object, parameter, data);
}
/*----------------------------------------------------------------------------*/
static size_t serialRead(void *object, void *buffer, size_t length)
{
  return Serial->read(object, buffer, length);
}
/*----------------------------------------------------------------------------*/
static size_t serialWrite(void *object, const void *buffer, size_t length)
{
  struct Irda * const interface = object;
  size_t written;

  const IrqState state = irqSave();
  written = byteQueuePushArray(&interface->base.txQueue, buffer, length);
  irqRestore(state);

  return written;
}
