/*
 * memory/nor_defs.h
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_NOR_DEFS_H_
#define DPM_MEMORY_NOR_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <dpm/memory/flash_defs.h>
#include <xcore/helpers.h>
/*----------------------------------------------------------------------------*/
#define JEDEC_DEVICE_MICRON_M25P            0x20
#define JEDEC_DEVICE_MICRON_M25PE           0x80
#define JEDEC_DEVICE_MICRON_M25PX           0x71
#define JEDEC_DEVICE_MICRON_N25Q            0xBA
#define JEDEC_DEVICE_WINBOND_W25Q_IM_JM     0x70
#define JEDEC_DEVICE_WINBOND_W25Q_IM        0x80
#define JEDEC_DEVICE_WINBOND_W25Q_IN_IQ_JQ  0x40
#define JEDEC_DEVICE_WINBOND_W25Q_IQ        0x60
#define JEDEC_DEVICE_WINBOND_W25X           0x30

enum
{
  NOR_HAS_SPI        = 0x01,
  NOR_HAS_BLOCKS_4K  = 0x02,
  NOR_HAS_BLOCKS_32K = 0x04,
  NOR_HAS_DIO        = 0x08,
  NOR_HAS_QIO        = 0x10,
  NOR_HAS_DDR        = 0x20,
  NOR_HAS_XIP        = 0x40,
  NOR_HAS_QPI        = 0x80
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

uint16_t norGetCapabilitiesByJedecInfo(const struct JedecInfo *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_NOR_DEFS_H_ */
