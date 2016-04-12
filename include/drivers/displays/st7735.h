/*
 * drivers/displays/st7735.h
 * Copyright (C) 2016 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_DISPLAYS_ST7735_H_
#define DPM_DRIVERS_DISPLAYS_ST7735_H_
/*----------------------------------------------------------------------------*/
#include <stdbool.h>
#include <interface.h>
#include <pin.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const ST7735;
/*----------------------------------------------------------------------------*/
struct ST7735Config
{
  /** Mandatory: memory interface. */
  struct Interface *bus;
  /** Mandatory: pin used as Chip Select output. */
  pinNumber cs;
  /** Mandatory: pin used for display reset. */
  pinNumber reset;
  /** Mandatory: pin used as Register Select output. */
  pinNumber rs;
};
/*----------------------------------------------------------------------------*/
struct ST7735
{
  struct Interface parent;

  struct Interface *bus;

  /* Chip Select output */
  struct Pin cs;
  /* Reset pin */
  struct Pin reset;
  /* Register Select output used to distinguish command and data modes */
  struct Pin rs;

  bool gramActive;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_DISPLAYS_ST7735_H_ */
