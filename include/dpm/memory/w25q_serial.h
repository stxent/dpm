/*
 * memory/w25q_serial.h
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_W25Q_SERIAL_H_
#define DPM_MEMORY_W25Q_SERIAL_H_
/*----------------------------------------------------------------------------*/
#include <dpm/memory/w25.h>
#include <halm/pin.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const W25QSerial;

struct W25QSerialConfig
{
  /** Mandatory: SPI interface. */
  void *spi;
  /**
   * Optional: timer for periodic polling of busy flag during
   * erase and write operations. Timer is mandatory for zero-copy mode.
   */
  void *timer;
  /**
   * Optional: poll rate. If set to zero, a default poll rate of 100 Hz
   * will be used. Poll rate can't be higher than timer tick rate.
   */
  uint32_t poll;
  /** Optional: bit rate of the serial interface. */
  uint32_t rate;
  /** Mandatory: chip select output. */
  PinNumber cs;
  /** Optional: output driver strength. */
  enum W25DriverStrength strength;
};

struct W25QSerial
{
  struct Interface base;

  void (*callback)(void *);
  void *callbackArgument;

  /* Serial interface */
  struct Interface *spi;
  /* Timer for periodic polling of busy flag during erase and write commands */
  struct Timer *timer;
  /* Chip select output */
  struct Pin cs;

  /* Memory capacity */
  uint32_t capacity;
  /* Read and write position inside memory address space */
  uint32_t position;
  /* Bit rate of the serial interface */
  uint32_t rate;

  struct
  {
    /* Buffer address */
    uintptr_t buffer;
    /* Number of bytes to be written */
    size_t left;
    /* Total buffer length */
    size_t length;
    /* Memory address during write and erase opertions */
    uint32_t position;
    /* Non-blocking process state */
    uint8_t state;
  } context;

  /* Command buffer */
  uint8_t command[6];

  /* Enable blocking mode */
  bool blocking;
  /* Memory capacity exceeds 16 MiB */
  bool extended;
  /* Sub-sector erase is available */
  bool subsectors;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_W25Q_SERIAL_H_ */
