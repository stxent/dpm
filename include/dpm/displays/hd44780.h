/*
 * displays/hd44780.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DISPLAYS_HD44780_H_
#define DPM_DISPLAYS_HD44780_H_
/*----------------------------------------------------------------------------*/
#include <dpm/displays/display.h>
#include <halm/pin.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const HD44780;

struct HD44780Config
{
  /** Mandatory: memory interface. */
  void *bus;
  /** Mandatory: display resolution. */
  struct DisplayResolution resolution;
  /** Mandatory: pin used as Register Select output. */
  PinNumber rs;
};

struct HD44780
{
  struct Interface parent;

  void (*callback)(void *);
  void *callbackArgument;

  struct Interface *bus;

  /* Display buffer */
  uint8_t *buffer;
  /* Command buffer */
  uint8_t command[4];
  /* Display resolution */
  struct DisplayResolution resolution;
  /* Active region of the display */
  struct DisplayWindow window;
  /* Register Select output used to distinguish command and data modes */
  struct Pin rs;

  /* Current line */
  uint8_t line;
  /* Current state of the display interface */
  uint8_t state;
  /* Flag indicating that display content should be updated */
  bool update;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DISPLAYS_HD44780_H_ */
