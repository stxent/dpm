/*
 * platform/lpc/ws281x_ssp.h
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_PLATFORM_LPC_WS281X_SSP_H_
#define DPM_PLATFORM_LPC_WS281X_SSP_H_
/*----------------------------------------------------------------------------*/
#include <halm/irq.h>
#include <halm/pin.h>
#include <halm/platform/lpc/ssp_base.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const WS281xSsp;

struct WS281xSspConfig
{
  /** Mandatory: number of LED in the LED strip. */
  size_t size;
  /** Mandatory: serial data output. */
  PinNumber mosi;
  /** Optional: interrupt priority. */
  IrqPriority priority;
  /** Mandatory: peripheral identifier. */
  uint8_t channel;
};

struct WS281xSsp
{
  struct SspBase base;

  void (*callback)(void *);
  void *callbackArgument;

  /* Pointer to an output buffer */
  uint16_t *buffer;
  /* Number of LED in the LED strip */
  size_t size;

  /* Current word in the output buffer */
  const uint16_t *txPosition;
  /* Number of bytes to be received */
  size_t rxLeft;
  /* Number of bytes to be transmitted */
  size_t txLeft;

  /* Transfer state */
  uint8_t state;
  /* Enable blocking mode */
  bool blocking;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_PLATFORM_LPC_WS281X_SSP_H_ */
