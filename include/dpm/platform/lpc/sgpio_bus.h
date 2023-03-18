/*
 * platform/lpc/sgpio_bus.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_PLATFORM_LPC_SGPIO_BUS_H_
#define DPM_PLATFORM_LPC_SGPIO_BUS_H_
/*----------------------------------------------------------------------------*/
#include <halm/irq.h>
#include <halm/pin.h>
#include <halm/platform/lpc/sgpio_base.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const SgpioBus;

struct SgpioBusConfig
{
  /** Mandatory: peripheral clock prescaler. */
  uint32_t prescaler;
  /** Optional: interrupt priority. */
  IrqPriority priority;
  /** Mandatory: DMA channel number. */
  uint8_t dma;
  /** Mandatory: enable clock signal inversion. */
  bool inversion;

  struct
  {
    /** Mandatory: clock output. */
    PinNumber clock;
    /** Mandatory: data outputs. */
    PinNumber data[8];

    /**
     * Mandatory: internal clock for DMA event timer. Possible values are
     * @b SGPIO_3 and @b SGPIO_12.
     */
    enum SgpioPin dma;
  } pins;

  struct
  {
    enum SgpioSlice gate;
    enum SgpioSlice qualifier;
  } slices;
};

struct SgpioBus
{
  struct SgpioBase base;

  void (*callback)(void *);
  void *callbackArgument;

  struct Dma *dma;
  struct Timer *timer;

  struct
  {
    uint8_t chain;
    uint8_t clock;
    uint8_t dma;
    uint8_t gate;
    uint8_t qualifier;
  } slices;

  /* Pointer to a buffer to be transmitted */
  const uint8_t *buffer;
  /* Number of bytes to be transmitted */
  uint32_t length;
  /* Selection between blocking mode and zero copy mode */
  bool blocking;
  /* Interface is busy transmitting data */
  bool busy;

  /* Peripheral clock prescaler */
  uint32_t prescaler;
  /* Slice count disable register mask */
  uint16_t controlDisableMask;
  /* Slice count enable register mask */
  uint16_t controlEnableMask;
  /* Enable clock signal inversion */
  bool inversion;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_PLATFORM_LPC_SGPIO_BUS_H_ */
