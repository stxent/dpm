/*
 * memory/flash_defs.h
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_FLASH_DEFS_H_
#define DPM_MEMORY_FLASH_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <stdint.h>
/*----------------------------------------------------------------------------*/
#define JEDEC_MANUFACTURER_MICRON           0x20
#define JEDEC_MANUFACTURER_MACRONIX         0xC2
#define JEDEC_MANUFACTURER_WINBOND          0xEF
/*----------------------------------------------------------------------------*/
struct [[gnu::packed]] JedecInfo
{
  uint8_t manufacturer;
  uint8_t type;
  uint8_t capacity;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_FLASH_DEFS_H_ */
