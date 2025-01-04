/*
 * nor_defs.c
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/memory/nor_defs.h>
#include <stddef.h>
/*----------------------------------------------------------------------------*/
struct NorCapabilityEntry
{
  uint8_t manufacturer;
  uint8_t type;
  uint16_t capabilities;
};
/*----------------------------------------------------------------------------*/
static const struct NorCapabilityEntry *findCapabilityEntry(
    const struct JedecInfo *);
/*----------------------------------------------------------------------------*/
static const struct NorCapabilityEntry norCapabilityMap[] = {
    {
        JEDEC_MANUFACTURER_MICRON,
        JEDEC_DEVICE_MICRON_M25P,
        NOR_HAS_SPI
    }, {
        JEDEC_MANUFACTURER_MICRON,
        JEDEC_DEVICE_MICRON_M25PE,
        NOR_HAS_SPI | NOR_HAS_BLOCKS_4K
    }, {
        JEDEC_MANUFACTURER_MICRON,
        JEDEC_DEVICE_MICRON_M25PX,
        NOR_HAS_SPI | NOR_HAS_BLOCKS_4K
    }, {
        JEDEC_MANUFACTURER_MICRON,
        JEDEC_DEVICE_MICRON_N25Q,
        NOR_HAS_SPI | NOR_HAS_BLOCKS_4K | NOR_HAS_BLOCKS_32K
            | NOR_HAS_DIO | NOR_HAS_QIO | NOR_HAS_XIP
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25Q_IM,
        NOR_HAS_SPI | NOR_HAS_BLOCKS_4K | NOR_HAS_BLOCKS_32K
            | NOR_HAS_DIO | NOR_HAS_QIO | NOR_HAS_DDR | NOR_HAS_XIP
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25Q_IM_JM,
        NOR_HAS_SPI | NOR_HAS_BLOCKS_4K | NOR_HAS_BLOCKS_32K
            | NOR_HAS_DIO | NOR_HAS_QIO | NOR_HAS_DDR | NOR_HAS_XIP
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25Q_IQ,
        NOR_HAS_SPI | NOR_HAS_BLOCKS_4K | NOR_HAS_BLOCKS_32K
            | NOR_HAS_DIO | NOR_HAS_QIO
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25Q_IN_IQ_JQ,
        NOR_HAS_SPI | NOR_HAS_BLOCKS_4K | NOR_HAS_BLOCKS_32K
            | NOR_HAS_DIO | NOR_HAS_QIO
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25X,
        NOR_HAS_SPI | NOR_HAS_BLOCKS_4K | NOR_HAS_BLOCKS_32K
            | NOR_HAS_DIO | NOR_HAS_XIP
    }
};
/*----------------------------------------------------------------------------*/
static const struct NorCapabilityEntry *findCapabilityEntry(
    const struct JedecInfo *info)
{
  for (size_t index = 0; index < ARRAY_SIZE(norCapabilityMap); ++index)
  {
    if (norCapabilityMap[index].manufacturer == info->manufacturer
        && norCapabilityMap[index].type == info->type)
    {
      return &norCapabilityMap[index];
    }
  }

  return NULL;
}
/*----------------------------------------------------------------------------*/
uint16_t norGetCapabilitiesByJedecInfo(const struct JedecInfo *info)
{
  const struct NorCapabilityEntry * const entry = findCapabilityEntry(info);
  return entry != NULL ? entry->capabilities : 0;
}
/*----------------------------------------------------------------------------*/
uint32_t norGetCapacityByJedecInfo(const struct JedecInfo *info)
{
  switch (info->capacity)
  {
    case JEDEC_CAPACITY_25Q016:
      return 2 * 1024 * 1024;

    case JEDEC_CAPACITY_25Q032:
      return 4 * 1024 * 1024;

    case JEDEC_CAPACITY_25Q064:
      return 8 * 1024 * 1024;

    case JEDEC_CAPACITY_25Q128:
      return 16 * 1024 * 1024;

    case JEDEC_CAPACITY_25Q256:
      return 32 * 1024 * 1024;

    case JEDEC_CAPACITY_25Q512:
      return 64 * 1024 * 1024;

    case JEDEC_CAPACITY_25Q01:
      return 128 * 1024 * 1024;

    case JEDEC_CAPACITY_25Q02:
      return 256 * 1024 * 1024;

    default:
      /* Unsupported capacity code */
      return 0;
  }
}
