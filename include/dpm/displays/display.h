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
enum DisplayOrientation
{
  DISPLAY_ORIENTATION_NORMAL,
  DISPLAY_ORIENTATION_MIRROR_X,
  DISPLAY_ORIENTATION_MIRROR_Y,
  DISPLAY_ORIENTATION_MIRROR_XY,

  DISPLAY_ORIENTATION_END
} __attribute__((packed));

/** Display options extending common interface options. */
enum IfDisplayParameter
{
  /** Update information on the display immediately. */
  IF_DISPLAY_UPDATE = IF_PARAMETER_END,

  /** Display orientation. */
  IF_DISPLAY_ORIENTATION,
  /** Display resolution. */
  IF_DISPLAY_RESOLUTION,
  /** Set first and last addresses of the current display window. */
  IF_DISPLAY_WINDOW
} __attribute__((packed));
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
