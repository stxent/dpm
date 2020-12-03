/*
 * irda.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/drivers/platform/lpc/irda.h>
#include <dpm/drivers/platform/lpc/irda_timer.h>
#include <halm/platform/lpc/gen_1/uart_defs.h>
/*----------------------------------------------------------------------------*/
#define FRAME_WIDTH  10
#define TX_FIFO_SIZE 16
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
static void onStartOfDataCallback(void *);
static void onStartOfFrameCallback(void *);
static void serialInterruptHandler(void *);
/*----------------------------------------------------------------------------*/
static enum Result serialInit(void *, const void *);
static void serialSetCallback(void *, void (*)(void *), void *);
static enum Result serialGetParam(void *, int, void *);
static enum Result serialSetParam(void *, int, const void *);
static size_t serialRead(void *, void *, size_t);
static size_t serialWrite(void *, const void *, size_t);

#ifndef CONFIG_PLATFORM_LPC_UART_NO_DEINIT
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
  return FRAME_WIDTH * (master ? ((width + 2) * 2) : (width + 2));
}
/*----------------------------------------------------------------------------*/
static void onStartOfDataCallback(void *object)
{
  struct Irda * const interface = object;
  LPC_UART_Type * const reg = interface->base.base.reg;

  /* Reconfigure to normal mode */
  reg->LCR &= ~(LCR_PE | LCR_PS_MASK);

  const size_t rxQueueSize = byteQueueSize(&interface->base.rxQueue);
  const size_t txQueueSize = byteQueueSize(&interface->base.txQueue);

  if (rxQueueSize || txQueueSize)
  {
    interface->pending = MIN(txQueueSize, interface->width);
    interface->state = STATE_DATA;
    irqSetPending(interface->base.base.irq);
  }
  else
    interface->state = STATE_IDLE;
}
/*----------------------------------------------------------------------------*/
static void onStartOfFrameCallback(void *object)
{
  struct Irda * const interface = object;

  if (interface->master || !byteQueueEmpty(&interface->base.txQueue))
  {
    LPC_UART_Type * const reg = interface->base.base.reg;

    /* Generate Break character */
    reg->LCR = (reg->LCR & ~LCR_PS_MASK) | (LCR_PE | LCR_PS(PS_FORCED_LOW));
    reg->THR = 0;

    interface->state = STATE_BREAK;
  }
  else
    interface->state = STATE_IDLE;
}
/*----------------------------------------------------------------------------*/
static void serialInterruptHandler(void *object)
{
  struct Irda * const interface = object;
  LPC_UART_Type * const reg = interface->base.base.reg;
  const uint32_t iir = reg->IIR & IIR_INTID_MASK;
  uint32_t lsr = reg->LSR;
  bool event = false;

  /* Read received frames */
  while (lsr & LSR_RDR)
  {
    const uint8_t data = reg->RBR;

    if (lsr & LSR_BI)
    {
      /* Break condition received */
      if (!interface->master)
      {
        /* Restart the timer */
        timerEnable(interface->timer);

        interface->state = STATE_WAIT;
      }
    }
    else if (!(lsr & LSR_FE))
    {
      /* Received bytes will be dropped when queue becomes full */
      if (!byteQueueFull(&interface->base.rxQueue))
        byteQueuePushBack(&interface->base.rxQueue, data);
    }
    else
      ++interface->fe;

    lsr = reg->LSR;
  }

  /* Send queued data */
  if (interface->state == STATE_DATA)
  {
    /*
     * Invoke user callback at the start of the frame when there is
     * an unread data in the reception buffer.
     */
    if (!byteQueueEmpty(&interface->base.rxQueue))
      event = true;

    /* Send the rest of the packet */
    if (lsr & LSR_THRE)
    {
      size_t bytesToWrite = MIN(TX_FIFO_SIZE, interface->pending);

      interface->pending -= bytesToWrite;
      if (!interface->pending)
        interface->state = STATE_IDLE;

      while (bytesToWrite--)
        reg->THR = byteQueuePopFront(&interface->base.txQueue);
    }
  }

  /* Handle line timeout */
  if (iir == IIR_INTID_CTI && !byteQueueEmpty(&interface->base.rxQueue))
  {
    event = true;
  }

  if (interface->base.callback && event)
    interface->base.callback(interface->base.callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void timerInterruptHandler(void *object)
{
  const struct IrdaTimerEvent * const event = object;

  if (event->type == IRDA_TIMER_SYNC)
    onStartOfFrameCallback(event->argument);
  else
    onStartOfDataCallback(event->argument);
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

  const struct IrdaTimerConfig timerConfig = {
      .frequency = config->rate,
      .period = calcTimerPeriod(config->frameLength, config->master),
      .sync = FRAME_WIDTH + 2,
      .priority = config->priority + 1,
      .channel = config->timer,
      .master = config->master
  };
  if (!(interface->timer = init(IrdaTimer, &timerConfig)))
    return E_ERROR;

  interface->base.base.handler = serialInterruptHandler;
  interface->pending = 0;
  interface->width = config->frameLength;
  interface->fe = 0;
  interface->master = config->master;
  interface->state = STATE_IDLE;

  timerSetCallback(interface->timer, timerInterruptHandler, interface);

  LPC_UART_Type * const reg = interface->base.base.reg;
  uint32_t ier = IER_RBRINTEN | IER_THREINTEN;

  if (!interface->master)
    ier |= IER_RLSINTEN;

  reg->ICR = ICR_IRDAEN | (config->inversion ? ICR_IRDAINV : 0);
  reg->IER = ier;

  if (interface->master)
    timerEnable(interface->timer);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
#ifndef CONFIG_PLATFORM_LPC_UART_NO_DEINIT
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
  struct Irda * const interface = object;

  switch ((enum SerialParameter)parameter)
  {
    case IF_SERIAL_FE:
      *(uint32_t *)data = interface->fe;
      return E_OK;

    default:
      break;
  }

  return Serial->getParam(interface, parameter, data);
}
/*----------------------------------------------------------------------------*/
static enum Result serialSetParam(void *object, int parameter, const void *data)
{
  struct Irda * const interface = object;

  switch ((enum SerialParameter)parameter)
  {
    case IF_SERIAL_FE:
      /* Reset the counter, data argument is ignored */
      interface->fe = 0;
      return E_OK;

    default:
      break;
  }

  return Serial->setParam(interface, parameter, data);
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

  irqDisable(interface->base.base.irq);
  written = byteQueuePushArray(&interface->base.txQueue, buffer, length);
  irqEnable(interface->base.base.irq);

  return written;
}
