/*
 * drivers/displays/display.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_DISPLAYS_DISPLAY_H_
#define DPM_DRIVERS_DISPLAYS_DISPLAY_H_
/*----------------------------------------------------------------------------*/
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
enum displayOrientation
{
  DISPLAY_ORIENTATION_NORMAL,
  DISPLAY_ORIENTATION_MIRROR_X,
  DISPLAY_ORIENTATION_MIRROR_Y,
  DISPLAY_ORIENTATION_MIRROR_XY,

  DISPLAY_ORIENTATION_END
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
  struct {
    uint16_t x;
    uint16_t y;
  } begin;

  struct {
    uint16_t x;
    uint16_t y;
  } end;
};
/*----------------------------------------------------------------------------*/
/** Display options extending common interface options. */
enum ifDisplayOption
{
  /** Update information on the display immediately. */
  IF_DISPLAY_UPDATE = IF_OPTION_END,

  /** Display orientation. */
  IF_DISPLAY_ORIENTATION,
  /** Display resolution. */
  IF_DISPLAY_RESOLUTION,
  /** Set first and last addresses of the current display window. */
  IF_DISPLAY_WINDOW
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_DISPLAYS_DISPLAY_H_ */
