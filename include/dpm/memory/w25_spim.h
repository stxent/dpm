/*
 * memory/w25_spim.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_W25_SPIM_H_
#define DPM_MEMORY_W25_SPIM_H_
/*----------------------------------------------------------------------------*/
#include <xcore/interface.h>
#include <stdbool.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
extern const struct InterfaceClass * const W25SPIM;

struct W25SPIMConfig
{
  /** Mandatory: underlying SPIM interface. */
  void *spim;
  /** Optional: force 3-byte memory addresses. */
  bool shrink;
  /** Optional: allow DTR mode. */
  bool dtr;
  /** Optional: allow QPI mode. */
  bool qpi;
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
    /* Buffer length */
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
  /* Enable 4-byte address mode */
  bool extended;
  /* Enable QPI mode */
  bool qpi;
  /* Enable QUAD IO mode */
  bool quad;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void w25MemoryMappingDisable(struct W25SPIM *);
void w25MemoryMappingEnable(struct W25SPIM *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_W25_SPIM_H_ */
