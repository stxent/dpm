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
  uint32_t offset;

  /** Mandatory: geometry of the flash memory regions. */
  const struct FlashGeometry *geometry;
  /** Mandatory: number of the flash memory regions. */
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
  uint32_t flashOffset;
  uint32_t flashSize;

  const struct FlashGeometry *geometry;
  size_t regions;

  /* Temporary buffer for received data */
  uint8_t *buffer;
  /* Buffer fill level, buffer should be filled to the writeChunkSize value */
  size_t bufferLevel;
  /* Minimal memory block size that can be used for write operations */
  size_t writeChunkSize;
  /* Current position for block erase operation */
  uint32_t erasePosition;
  /* Current position for buffer write operation */
  uint32_t writePosition;
  /* Erase type, memory can be erased in pages, sectors or blocks */
  uint8_t eraseType;
  /* Erase operation is pending */
  bool eraseQueued;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_USB_DFU_BRIDGE_H_ */
