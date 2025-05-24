/*
 * displays/display.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DISPLAYS_DISPLAY_H_
#define DPM_DISPLAYS_DISPLAY_H_
/*----------------------------------------------------------------------------*/
#include <xcore/interface.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
enum [[gnu::packed]] DisplayOrientation
{
  DISPLAY_ORIENTATION_NORMAL,
  DISPLAY_ORIENTATION_MIRROR_X,
  DISPLAY_ORIENTATION_MIRROR_Y,
  DISPLAY_ORIENTATION_MIRROR_XY,

  DISPLAY_ORIENTATION_END
};

/** Display options extending common interface options. */
enum [[gnu::packed]] DisplayParameter
{
  /** Update information on the display immediately. */
  IF_DISPLAY_UPDATE = IF_PARAMETER_END,

  /**
   * Display orientation. Parameter type is \a uint8_t. Possible values
   * are described in the \a DisplayOrientation enumeration.
   */
  IF_DISPLAY_ORIENTATION,
  /** Display resolution. Parameter type is \a DisplayResolution structure. */
  IF_DISPLAY_RESOLUTION,
  /**
   * First and last addresses of the current display window.
   * Parameter type is \a DisplayWindow structure.
   */
  IF_DISPLAY_WINDOW
};
/*----------------------------------------------------------------------------*/
struct DisplayPoint
{
  uint16_t x;
  uint16_t y;
};

struct DisplayResolution
{
  uint16_t width;
  uint16_t height;
};

struct DisplayWindow
{
  uint16_t ax;
  uint16_t ay;
  uint16_t bx;
  uint16_t by;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DISPLAYS_DISPLAY_H_ */
