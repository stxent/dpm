/*
 * platform/stm32/irda.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_PLATFORM_STM32_IRDA_H_
#define DPM_PLATFORM_STM32_IRDA_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/stm32/uart_base.h>
#include <halm/timer.h>
#include <xcore/containers/byte_queue.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const Irda;

struct IrdaConfig
{
  /** Mandatory: baud rate. */
  uint32_t rate;
  /** Mandatory: input queue size. */
  size_t rxLength;
  /** Mandatory: output queue size. */
  size_t txLength;
  /** Mandatory: frame length. */
  size_t frameLength;
  /** Mandatory: serial input. */
  PinNumber rx;
  /** Mandatory: serial output. */
  PinNumber tx;
  /** Optional: interrupt priority. */
  IrqPriority priority;
  /** Mandatory: UART peripheral identifier. */
  uint8_t channel;
  /** Mandatory: Timer peripheral identifier. */
  uint8_t timer;
  /** Mandatory: mode select. */
  bool master;
};

struct Irda
{
  struct UartBase base;

  void (*callback)(void *);
  void *callbackArgument;

  /* Input queue */
  struct ByteQueue rxQueue;
  /* Output queue */
  struct ByteQueue txQueue;

  /* Frame timer */
  struct Timer *timer;
  /* Pending bytes in the current frame */
  size_t pending;
  /* Frame length in bytes */
  size_t width;
  /* Current state */
  uint8_t state;
  /* Enable master mode */
  bool master;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_PLATFORM_STM32_IRDA_H_ */
