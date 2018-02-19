/*
 * drivers/displays/s6d1121.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_DISPLAYS_S6D1121_H_
#define DPM_DRIVERS_DISPLAYS_S6D1121_H_
/*----------------------------------------------------------------------------*/
#include <stdbool.h>
#include <halm/pin.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const S6D1121;
/*----------------------------------------------------------------------------*/
struct S6D1121Config
{
  /** Mandatory: memory interface. */
  struct Interface *bus;
  /** Optional: pin used as Chip Select output. */
  PinNumber cs;
  /** Mandatory: pin used for display reset. */
  PinNumber reset;
  /** Mandatory: pin used as Register Select output. */
  PinNumber rs;
};
/*----------------------------------------------------------------------------*/
struct S6D1121
{
  struct Interface parent;

  struct Interface *bus;

  /* Chip Select output */
  struct Pin cs;
  /* Reset pin */
  struct Pin reset;
  /* Register Select output used to distinguish command and data modes */
  struct Pin rs;

  /* Chip Select pin is controlled externally */
  bool csExternal;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_DISPLAYS_S6D1121_H_ */
