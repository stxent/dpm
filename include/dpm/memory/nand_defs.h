/*
 * memory/nand_defs.h
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_NAND_DEFS_H_
#define DPM_MEMORY_NAND_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <dpm/memory/flash_defs.h>
#include <xcore/helpers.h>
/*----------------------------------------------------------------------------*/
#define JEDEC_DEVICE_WINBOND_W25N_GV        0xAA
#define JEDEC_DEVICE_WINBOND_W25N_GW        0xBA
#define JEDEC_DEVICE_WINBOND_W25N_JW        0xBC
#define JEDEC_DEVICE_WINBOND_W25N_KV        0xAE
#define JEDEC_DEVICE_WINBOND_W25N_KW        0xBE

enum
{
  NAND_HAS_SPI          = 0x01,
  NAND_HAS_DIO          = 0x02,
  NAND_HAS_QIO          = 0x04,
  NAND_HAS_DDR          = 0x08
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

uint16_t nandGetCapabilitiesByJedecInfo(const struct JedecInfo *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_NAND_DEFS_H_ */
