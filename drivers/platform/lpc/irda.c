/*
 * irda.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/platform/lpc/irda.h>
#include <dpm/platform/lpc/irda_timer.h>
#include <halm/generic/byte_queue_extensions.h>
#include <halm/platform/lpc/gen_1/uart_defs.h>
#include <string.h>
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
  LPC_UART_Type * const reg = interface->base.reg;

  /* Reconfigure to normal mode */
  reg->LCR &= ~(LCR_PE | LCR_PS_MASK);

  const size_t rxQueueSize = byteQueueSize(&interface->rxQueue);
  const size_t txQueueSize = byteQueueSize(&interface->txQueue);

  if (rxQueueSize || txQueueSize)
  {
    interface->pending = MIN(txQueueSize, interface->width);
    interface->state = STATE_DATA;
    irqSetPending(interface->base.irq);
  }
  else
    interface->state = STATE_IDLE;
}
/*----------------------------------------------------------------------------*/
static void onStartOfFrameCallback(void *object)
{
  struct Irda * const interface = object;

  if (interface->master || !byteQueueEmpty(&interface->txQueue))
  {
    LPC_UART_Type * const reg = interface->base.reg;

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
  LPC_UART_Type * const reg = interface->base.reg;
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
      if (!byteQueueFull(&interface->rxQueue))
        byteQueuePushBack(&interface->rxQueue, data);
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
    if (!byteQueueEmpty(&interface->rxQueue))
      event = true;

    /* Send the rest of the packet */
    if (lsr & LSR_THRE)
    {
      size_t bytesToWrite = MIN(TX_FIFO_SIZE, interface->pending);

      interface->pending -= bytesToWrite;
      if (!interface->pending)
        interface->state = STATE_IDLE;

      while (bytesToWrite--)
        reg->THR = byteQueuePopFront(&interface->txQueue);
    }
  }

  /* Handle line timeout */
  if (iir == IIR_INTID_CTI && !byteQueueEmpty(&interface->rxQueue))
  {
    event = true;
  }

  if (event && interface->callback != NULL)
    interface->callback(interface->callbackArgument);
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
  assert(config != NULL);
  assert(config->frameLength <= config->rxLength);

  const struct UartBaseConfig baseConfig = {
      .rx = config->rx,
      .tx = config->tx,
      .channel = config->channel
  };
  struct Irda * const interface = object;
  struct UartRateConfig rateConfig;
  enum Result res;

  /* Call base class constructor */
  if ((res = UartBase->init(object, &baseConfig)) != E_OK)
    return res;

  if ((res = uartCalcRate(object, config->rate, &rateConfig)) != E_OK)
    return res;

  if (!byteQueueInit(&interface->rxQueue, config->rxLength))
    return E_MEMORY;
  if (!byteQueueInit(&interface->txQueue, config->txLength))
    return E_MEMORY;

  uartSetRate(object, rateConfig);

  const struct IrdaTimerConfig timerConfig = {
      .frequency = config->rate,
      .period = calcTimerPeriod(config->frameLength, config->master),
      .sync = FRAME_WIDTH + 2,
      .priority = config->priority + 1,
      .channel = config->timer,
      .master = config->master
  };

  interface->timer = init(IrdaTimer, &timerConfig);
  if (interface->timer == NULL)
    return E_ERROR;

  interface->base.handler = serialInterruptHandler;
  interface->callback = NULL;
  interface->pending = 0;
  interface->width = config->frameLength;
  interface->fe = 0;
  interface->master = config->master;
  interface->state = STATE_IDLE;

  timerSetCallback(interface->timer, timerInterruptHandler, interface);

  LPC_UART_Type * const reg = interface->base.reg;
  uint32_t ier = IER_RBRINTEN | IER_THREINTEN;

  /* Set 8-bit length */
  reg->LCR = LCR_WLS(WLS_8BIT);
  /* Enable FIFO and set RX trigger level */
  reg->FCR = (reg->FCR & ~FCR_RXTRIGLVL_MASK) | FCR_FIFOEN
      | FCR_RXTRIGLVL(RX_TRIGGER_LEVEL_8);
  /* Enable RBR and THRE interrupts */
  reg->IER = IER_RBRINTEN | IER_THREINTEN;
  /* Transmitter is enabled by default thus TER register is left untouched */

  if (!interface->master)
    ier |= IER_RLSINTEN;

  reg->ICR = ICR_IRDAEN | (config->inversion ? ICR_IRDAINV : 0);
  reg->IER = ier;

  irqSetPriority(interface->base.irq, config->priority);
  irqEnable(interface->base.irq);

  if (interface->master)
    timerEnable(interface->timer);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
#ifndef CONFIG_PLATFORM_LPC_UART_NO_DEINIT
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

  switch ((enum SerialParameter)parameter)
  {
    case IF_SERIAL_FE:
      *(uint32_t *)data = interface->fe;
      return E_OK;

    default:
      break;
  }

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
static enum Result serialSetParam(void *object, int parameter,
    const void *data __attribute__((unused)))
{
  struct Irda * const interface = object;

  switch ((enum SerialParameter)parameter)
  {
    case IF_SERIAL_FE:
      /* Reset the counter, data argument is ignored */
      interface->fe = 0;
      return E_OK;

    default:
      return E_INVALID;
  }
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
