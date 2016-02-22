/*
 * drivers/displays/display.h
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DRIVERS_DISPLAYS_DISPLAY_H_
#define DRIVERS_DISPLAYS_DISPLAY_H_
/*----------------------------------------------------------------------------*/
#include <interface.h>
/*----------------------------------------------------------------------------*/
/** Display options extending common interface options. */
enum ifDisplayOption
{
  /** Update information on the display immediately. */
  IF_DISPLAY_UPDATE = IF_OPTION_END,

  /** Display width. */
  IF_DISPLAY_WIDTH,
  /** Display height. */
  IF_DISPLAY_HEIGHT,
  /** First address of the current display window. */
  IF_DISPLAY_WINDOW_BEGIN,
  /** Last address of the current display window. */
  IF_DISPLAY_WINDOW_END
};
/*----------------------------------------------------------------------------*/
#endif /* DRIVERS_DISPLAYS_DISPLAY_H_ */
