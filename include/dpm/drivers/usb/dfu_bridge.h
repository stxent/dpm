/*
 * drivers/usb/dfu_bridge.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_USB_DFU_BRIDGE_H_
#define DPM_DRIVERS_USB_DFU_BRIDGE_H_
/*----------------------------------------------------------------------------*/
#include <halm/usb/dfu.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const DfuBridge;

struct FlashGeometry
{
  /** Sector count in a flash region. */
  size_t count;
  /** Size of each sector in a region. */
  size_t size;
  /** Sector erase time in milliseconds. */
  uint32_t time;
};

struct DfuBridgeConfig
{
  /** Mandatory: geometry of the flash memory. */
  const struct FlashGeometry *geometry;
  /** Mandatory: flash memory interface. */
  struct Interface *flash;
  /** Mandatory: DFU instance. */
  struct Dfu *device;
  /** Mandatory: offset from the beginning of the flash memory. */
  size_t offset;
  /** Optional: software reset handler. */
  void (*reset)(void);
  /** Optional: disable firmware reading. */
  bool writeonly;
};

struct DfuBridge
{
  struct Entity base;

  struct Interface *flash;
  struct Dfu *device;
  void (*reset)(void);

  const struct FlashGeometry *geometry;
  uint8_t *chunk;

  size_t flashOffset;
  size_t flashSize;
  size_t pageSize;

  size_t bufferSize;
  size_t currentPosition;

  size_t erasingPosition;
  bool eraseQueued;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_USB_DFU_BRIDGE_H_ */
