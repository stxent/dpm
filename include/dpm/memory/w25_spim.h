/*
 * memory/w25_spim.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_W25_SPIM_H_
#define DPM_MEMORY_W25_SPIM_H_
/*----------------------------------------------------------------------------*/
#include <dpm/memory/w25.h>
#include <xcore/interface.h>
#include <stdbool.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const W25SPIM;

struct W25SPIMConfig
{
  /** Mandatory: underlying SPIM interface. */
  void *spim;
  /** Optional: output driver strength. */
  enum W25DriverStrength strength;
  /** Optional: allow DTR mode. */
  bool dtr;
  /** Optional: force 3-byte memory addresses in memory-mapped mode. */
  bool shrink;
  /** Optional: allow XIP mode. */
  bool xip;
};

struct W25SPIM
{
  struct Interface base;

  void (*callback)(void *);
  void *callbackArgument;

  /* Memory interface */
  struct Interface *spim;
  /* Memory capacity */
  uint32_t capacity;
  /* Read and write position inside memory address space */
  uint32_t position;

  struct
  {
    /* Buffer address */
    const void *buffer;
    /* Number of bytes to be written */
    size_t left;
    /* Total buffer length */
    size_t length;
    /* Memory address during write and erase opertions */
    uint32_t position;
    /* Non-blocking process state */
    uint8_t state;
  } context;

  /* Enable blocking mode */
  bool blocking;
  /* Enable DTR mode */
  bool dtr;
  /* Memory capacity exceeds 16 MiB */
  bool extended;
  /* Enable QUAD IO mode */
  bool quad;
  /* Force 3-byte memory addresses in memory-mapped mode. */
  bool shrink;
  /* Enable XIP or Continuous Read mode */
  bool xip;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void w25MemoryMappingDisable(struct W25SPIM *);
void w25MemoryMappingEnable(struct W25SPIM *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_W25_SPIM_H_ */
