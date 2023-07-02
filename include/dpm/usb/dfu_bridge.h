/*
 * usb/dfu_bridge.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_USB_DFU_BRIDGE_H_
#define DPM_USB_DFU_BRIDGE_H_
/*----------------------------------------------------------------------------*/
#include <halm/usb/dfu.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const DfuBridge;

struct DfuBridgeConfig
{
  /** Mandatory: DFU instance. */
  struct Dfu *device;
  /** Optional: software reset handler. */
  void (*reset)(void);

  /** Mandatory: flash memory interface. */
  void *flash;
  /** Mandatory: offset from the beginning of the flash memory. */
  size_t offset;

  /** Mandatory: geometry of the flash memory regions. */
  const struct FlashGeometry *geometry;
  /** Mandatory: count of the flash memory regions. */
  size_t regions;

  /** Optional: disable firmware reading. */
  bool writeonly;
};

struct DfuBridge
{
  struct Entity base;

  struct Dfu *device;
  void (*reset)(void);

  struct Interface *flash;
  size_t flashOffset;
  size_t flashSize;

  const struct FlashGeometry *geometry;
  size_t regions;

  uint8_t *chunkData;
  size_t chunkSize;

  size_t bufferSize;
  size_t currentPosition;

  size_t erasingPosition;
  bool eraseQueued;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_USB_DFU_BRIDGE_H_ */
