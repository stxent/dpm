/*
 * irda.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/stm32/irda.h>
#include <halm/platform/stm32/gptimer.h>
#include <halm/platform/stm32/uart_defs.h>
#include <xcore/containers/byte_queue.h>
#include <string.h>
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
#  define serialDeinit deletedDestructorTrap
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
  STM_USART_Type * const reg = interface->base.reg;
  uint32_t control = reg->CR1;
  uint32_t status = reg->SR;
  bool event = false;

  /* Handle reception timeout */
  if (status & SR_IDLE)
  {
    /* Software sequence to clear IDLE flag */
    (void)reg->DR;

    if (!byteQueueEmpty(&interface->rxQueue))
      event = true;
  }

  if (interface->state == STATE_BREAK)
  {
    control &= ~CR1_TCIE;

    /*
     * Invoke user callback at the start of the frame when there is
     * an unread data in the reception buffer.
     */
    if (!byteQueueEmpty(&interface->rxQueue))
    {
      event = true;
    }

    if (!byteQueueEmpty(&interface->txQueue))
    {
      const size_t txQueueSize = byteQueueSize(&interface->txQueue);

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
      if (!byteQueueFull(&interface->rxQueue))
        byteQueuePushBack(&interface->rxQueue, data);
    }
  }

  /* Send remaining data */
  if ((control & CR1_TXEIE) && (status & SR_TXE))
  {
    reg->DR = byteQueuePopFront(&interface->txQueue);

    if (!--interface->pending)
    {
      control &= ~CR1_TXEIE;
      interface->state = STATE_IDLE;
    }
  }

  reg->CR1 = control;

  if (event && interface->callback != NULL)
    interface->callback(interface->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void timerInterruptHandler(void *object)
{
  struct Irda * const interface = object;

  if (interface->master || !byteQueueEmpty(&interface->txQueue))
  {
    STM_USART_Type * const reg = interface->base.reg;

    /* Generate Break character */
    reg->SR &= ~SR_TC;
    reg->CR1 |= CR1_SBK | CR1_TCIE;

    interface->state = STATE_BREAK;
  }
  else if (!byteQueueEmpty(&interface->rxQueue))
  {
    interface->state = STATE_BREAK;
    irqSetPending(interface->base.irq);
  }

  if (!interface->master)
    timerDisable(interface->timer);
}
/*----------------------------------------------------------------------------*/
static enum Result serialInit(void *object, const void *configBase)
{
  const struct IrdaConfig * const config = configBase;
  assert(config != NULL);
  assert(config->frameLength <= config->rxLength);

  const struct UartBaseConfig baseConfig = {
      .rx = config->rx,
      .tx = config->tx,
      .channel = config->channel
  };
  struct Irda * const interface = object;
  enum Result res;

  /* Call base class constructor */
  if ((res = UartBase->init(interface, &baseConfig)) != E_OK)
    return res;

  if (!byteQueueInit(&interface->rxQueue, config->rxLength))
    return E_MEMORY;
  if (!byteQueueInit(&interface->txQueue, config->txLength))
    return E_MEMORY;

  const struct GpTimerConfig timerConfig = {
      .frequency = config->rate,
      .priority = config->priority,
      .channel = config->timer,
      .event = 0
  };
  if (!(interface->timer = init(GpTimer, &timerConfig)))
    return E_ERROR;

  interface->base.handler = serialInterruptHandler;
  interface->callback = NULL;
  interface->pending = 0;
  interface->width = config->frameLength;
  interface->master = config->master;
  interface->state = STATE_IDLE;

  timerSetCallback(interface->timer, timerInterruptHandler, interface);
  timerSetOverflow(interface->timer, calcTimerPeriod(interface->width,
      interface->master));

  STM_USART_Type * const reg = interface->base.reg;

  /* Disable the peripheral */
  reg->CR1 = 0;

  uartSetRate(object, config->rate);

  /* Enable Break interrupt */
  reg->CR2 |= CR2_LBDIE;
  /* Enable IrDA */
  reg->CR3 |= CR3_IREN;
  /* In normal IrDA mode prescaler must be set to 1 */
  reg->GTPR = GTPR_PSC(1);

  /*
   * Enable receiver and transmitter, RXNE and IDLE interrupts,
   * enable peripheral.
   */
  reg->CR1 |= CR1_RE | CR1_TE | CR1_RXNEIE | CR1_IDLEIE | CR1_UE;

  irqSetPriority(interface->base.irq, config->priority);
  irqEnable(interface->base.irq);

  if (interface->master)
  {
    timerSetAutostop(interface->timer, false);
    timerEnable(interface->timer);
  }
  else
    timerSetAutostop(interface->timer, true);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
#ifndef CONFIG_PLATFORM_STM32_UART_NO_DEINIT
static void serialDeinit(void *object)
{
  struct Irda * const interface = object;

  irqDisable(interface->base.irq);

  deinit(interface->timer);
  byteQueueDeinit(&interface->txQueue);
  byteQueueDeinit(&interface->rxQueue);
  UartBase->deinit(interface);
}
#endif
/*----------------------------------------------------------------------------*/
static void serialSetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct Irda * const interface = object;

  interface->callbackArgument = argument;
  interface->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result serialGetParam(void *object, int parameter, void *data)
{
  struct Irda * const interface = object;

  switch ((enum IfParameter)parameter)
  {
    case IF_RX_AVAILABLE:
      *(size_t *)data = byteQueueSize(&interface->rxQueue);
      return E_OK;

    case IF_RX_PENDING:
      *(size_t *)data = byteQueueCapacity(&interface->rxQueue)
          - byteQueueSize(&interface->rxQueue);
      return E_OK;

    case IF_TX_AVAILABLE:
      *(size_t *)data = byteQueueCapacity(&interface->txQueue)
          - byteQueueSize(&interface->txQueue);
      return E_OK;

    case IF_TX_PENDING:
      *(size_t *)data = byteQueueSize(&interface->txQueue);
      return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result serialSetParam(void *object __attribute__((unused)),
    int parameter __attribute__((unused)),
    const void *data __attribute__((unused)))
{
  return E_INVALID;
}
/*----------------------------------------------------------------------------*/
static size_t serialRead(void *object, void *buffer, size_t length)
{
  struct Irda * const interface = object;
  uint8_t *position = buffer;

  while (length && !byteQueueEmpty(&interface->rxQueue))
  {
    const uint8_t *address;
    size_t count;

    byteQueueDeferredPop(&interface->rxQueue, &address, &count, 0);
    count = MIN(length, count);
    memcpy(position, address, count);

    irqDisable(interface->base.irq);
    byteQueueAbandon(&interface->rxQueue, count);
    irqEnable(interface->base.irq);

    position += count;
    length -= count;
  }

  return position - (uint8_t *)buffer;
}
/*----------------------------------------------------------------------------*/
static size_t serialWrite(void *object, const void *buffer, size_t length)
{
  struct Irda * const interface = object;
  const uint8_t *position = buffer;

  while (length && !byteQueueFull(&interface->txQueue))
  {
    uint8_t *address;
    size_t count;

    byteQueueDeferredPush(&interface->txQueue, &address, &count, 0);
    count = MIN(length, count);
    memcpy(address, position, count);

    irqDisable(interface->base.irq);
    byteQueueAdvance(&interface->txQueue, count);
    irqEnable(interface->base.irq);

    position += count;
    length -= count;
  }

  return position - (const uint8_t *)buffer;
}
