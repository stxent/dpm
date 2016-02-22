/*
 * s6d1121.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef S6D1121_H_
#define S6D1121_H_
/*----------------------------------------------------------------------------*/
#include <stdbool.h>
#include <interface.h>
#include <pin.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass *S6D1121;
/*----------------------------------------------------------------------------*/
struct S6D1121Config
{
  /** Mandatory: memory interface. */
  struct Interface *bus;
  /** Mandatory: pin used as Chip Select output. */
  pinNumber cs;
  /** Mandatory: pin used as Register Select output. */
  pinNumber rs;
};
/*----------------------------------------------------------------------------*/
struct S6D1121
{
  struct Interface parent;

  void (*callback)(void *);
  void *callbackArgument;

  struct Interface *bus;

  /* Chip Select output */
  struct Pin cs;
  /* Register Select output used to distinguish command and data modes */
  struct Pin rs;

  /* Buffered values for the starting point of the display window */
  struct {
    uint16_t x;
    uint16_t y;
  } window;
};
/*----------------------------------------------------------------------------*/
#endif /* S6D1121_H_ */
