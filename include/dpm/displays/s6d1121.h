/*
 * displays/s6d1121.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DISPLAYS_S6D1121_H_
#define DPM_DISPLAYS_S6D1121_H_
/*----------------------------------------------------------------------------*/
#include <halm/pin.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const S6D1121;
/*----------------------------------------------------------------------------*/
struct S6D1121Config
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

  /* Current window size */
  struct DisplayWindow window;
  /* Current orientation */
  uint8_t orientation;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DISPLAYS_S6D1121_H_ */
