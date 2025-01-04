/*
 * memory/nor_defs.h
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_NOR_DEFS_H_
#define DPM_MEMORY_NOR_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <xcore/bits.h>
#include <xcore/helpers.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
#define JEDEC_MANUFACTURER_MICRON           0x20
#define JEDEC_MANUFACTURER_WINBOND          0xEF

#define JEDEC_DEVICE_MICRON_M25P            0x20
#define JEDEC_DEVICE_MICRON_M25PE           0x80
#define JEDEC_DEVICE_MICRON_M25PX           0x71
#define JEDEC_DEVICE_MICRON_N25Q            0xBA
#define JEDEC_DEVICE_WINBOND_W25Q_IM_JM     0x70
#define JEDEC_DEVICE_WINBOND_W25Q_IM        0x80
#define JEDEC_DEVICE_WINBOND_W25Q_IN_IQ_JQ  0x40
#define JEDEC_DEVICE_WINBOND_W25Q_IQ        0x60
#define JEDEC_DEVICE_WINBOND_W25X           0x30

#define JEDEC_CAPACITY_25Q016               0x15  /* 16 Mbit */
#define JEDEC_CAPACITY_25Q032               0x16  /* 32 Mbit */
#define JEDEC_CAPACITY_25Q064               0x17  /* 64 Mbit */
#define JEDEC_CAPACITY_25Q128               0x18  /* 128 Mbit */
#define JEDEC_CAPACITY_25Q256               0x19  /* 256 Mbit */
#define JEDEC_CAPACITY_25Q512               0x20  /* 512 Mbit */
#define JEDEC_CAPACITY_25Q01                0x21  /* 1 Gbit */
#define JEDEC_CAPACITY_25Q02                0x22  /* 2 Gbit */
/*----------------------------------------------------------------------------*/
enum
{
  NOR_HAS_SPI        = 0x01,
  NOR_HAS_BLOCKS_4K  = 0x02,
  NOR_HAS_BLOCKS_32K = 0x04,
  NOR_HAS_DIO        = 0x08,
  NOR_HAS_QIO        = 0x10,
  NOR_HAS_DDR        = 0x20,
  NOR_HAS_XIP        = 0x40
};

struct [[gnu::packed]] JedecInfo
{
  uint8_t manufacturer;
  uint8_t type;
  uint8_t capacity;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

uint16_t norGetCapabilitiesByJedecInfo(const struct JedecInfo *);
uint32_t norGetCapacityByJedecInfo(const struct JedecInfo *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_NOR_DEFS_H_ */
