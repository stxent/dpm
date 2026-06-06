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
  /** Mandatory: pointer to the DFU instance. */
  struct Dfu *device;
  /**
   * Optional: software reset handler function. If provided, this function
   * will be called to perform a software reset of the device during
   * the DFU process. Can be set to NULL.
   */
  void (*reset)(void);
  /** Mandatory: pointer to the flash memory interface. */
  void *flash;
  /** Mandatory: offset from the beginning of the flash memory (in bytes). */
  uint32_t offset;
  /**
   * Mandatory: pointer to an array of FlashGeometry structures describing
   * the flash memory regions.
   */
  const struct FlashGeometry *geometry;
  /** Mandatory: number of elements in the FlashGeometry array. */
  size_t regions;
  /**
   * Optional: desired chunk size for data transfers (in bytes).
   * If set to 0 or left uninitialized, the system automatically selects
   * the chunk size based on the minimum memory block size. If the
   * specified size exceeds the minimum block size, it is capped at that size.
   */
  size_t chunk;
  /**
   * Optional: flag to disable firmware reading operations. When set
   * to @b true the DFU bridge will allow only firmware updates.
   */
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
