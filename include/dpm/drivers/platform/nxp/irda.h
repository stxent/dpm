/*
 * drivers/platform/nxp/irda.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_PLATFORM_NXP_IRDA_H_
#define DPM_DRIVERS_PLATFORM_NXP_IRDA_H_
/*----------------------------------------------------------------------------*/
#include <halm/platform/nxp/serial.h>
#include <halm/timer.h>
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
  /** Optional: enable input inversion. */
  bool inversion;
  /** Mandatory: mode select. */
  bool master;
};

struct Irda
{
  struct Serial base;

  /* Frame timer */
  struct Timer *timer;
  /* Pending bytes in the current frame */
  size_t pending;
  /* Frame length in bytes */
  size_t width;
  /* Frame error counter */
  uint32_t fe;
  /* Current state */
  uint8_t state;
  /* Enable master mode */
  bool master;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_PLATFORM_NXP_IRDA_H_ */
