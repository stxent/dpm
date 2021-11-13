/*
 * drivers/displays/ili9325.h
 * Copyright (C) 2021 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DRIVERS_DISPLAYS_ILI9325_H_
#define DPM_DRIVERS_DISPLAYS_ILI9325_H_
/*----------------------------------------------------------------------------*/
#include <halm/pin.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const ILI9325;
/*----------------------------------------------------------------------------*/
struct ILI9325Config
{
  /** Mandatory: memory interface. */
  struct Interface *bus;
  /** Mandatory: pin used as Chip Select output. */
  PinNumber cs;
  /** Mandatory: pin used for display reset. */
  PinNumber reset;
  /** Mandatory: pin used as Register Select output. */
  PinNumber rs;
};
/*----------------------------------------------------------------------------*/
struct ILI9325
{
  struct Interface parent;

  struct Interface *bus;

  /* Chip Select output */
  struct Pin cs;
  /* Reset pin */
  struct Pin reset;
  /* Register Select output used to distinguish command and data modes */
  struct Pin rs;

  /* Current window size */
  struct DisplayWindow window;
  /* Current orientation */
  uint8_t orientation;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_DISPLAYS_ILI9325_H_ */
