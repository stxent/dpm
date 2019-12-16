/*
 * drivers/usb/mouse.h
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_USB_MOUSE_H_
#define DPM_DRIVERS_USB_MOUSE_H_
/*----------------------------------------------------------------------------*/
#include <stdint.h>
#include <halm/usb/hid.h>
/*----------------------------------------------------------------------------*/
extern const struct HidClass * const Mouse;
struct Mouse;

struct MouseConfig
{
  /** Mandatory: USB device. */
  void *device;

  struct
  {
    /** Mandatory: identifier of the notification endpoint. */
    uint8_t interrupt;
  } endpoints;
};
/*----------------------------------------------------------------------------*/
void mouseMovePointer(struct Mouse *, int8_t, int8_t);
void mouseClick(struct Mouse *, uint8_t);
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_USB_MOUSE_H_ */
