/*
 * memory/mx35_quad.h
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_MX35_QUAD_H_
#define DPM_MEMORY_MX35_QUAD_H_
/*----------------------------------------------------------------------------*/
#include <halm/pin.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const MX35Quad;

struct MX35QuadConfig
{
  /** Mandatory: SPIM interface. */
  void *spim;
  /** Optional: enable internal ECC. */
  bool ecc;
  /** Optional: enable spare area for each page. */
  bool spare;
};

struct MX35Quad
{
  struct Interface base;

  void (*callback)(void *);
  void *callbackArgument;

  /* Serial interface */
  struct Interface *spim;

  /* Memory capacity */
  uint32_t capacity;
  /* Read and write position inside memory address space */
  uint32_t position;
  /* Page size in bytes */
  uint16_t page;

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
  /* Enable internal ECC */
  bool ecc;
  /* Enable QIO mode for address phase */
  bool qio;
  /* Enable QUAD mode */
  bool quad;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_MX35_QUAD_H_ */
