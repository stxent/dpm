/*
 * displays/st7735.h
 * Copyright (C) 2016 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DISPLAYS_ST7735_H_
#define DPM_DISPLAYS_ST7735_H_
/*----------------------------------------------------------------------------*/
#include <halm/pin.h>
#include <xcore/interface.h>
#include <stdbool.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const ST7735;

struct ST7735Config
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

  /* Current window size */
  struct DisplayWindow window;
  /* Current orientation */
  uint8_t orientation;
  /* GRAM register is selected */
  bool gramActive;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DISPLAYS_ST7735_H_ */
