/*
 * nand_defs.c
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/memory/nand_defs.h>
#include <stddef.h>
/*----------------------------------------------------------------------------*/
struct NandCapabilityEntry
{
  uint8_t manufacturer;
  uint8_t type;
  uint16_t capabilities;
};
/*----------------------------------------------------------------------------*/
static const struct NandCapabilityEntry *findCapabilityEntry(
    const struct JedecInfo *);
/*----------------------------------------------------------------------------*/
static const struct NandCapabilityEntry nandCapabilityMap[] = {
    {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25N_GV,
        NAND_HAS_SPI | NAND_HAS_DIO | NAND_HAS_QIO
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25N_GW,
        NAND_HAS_SPI | NAND_HAS_DIO | NAND_HAS_QIO
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25N_JW,
        NAND_HAS_SPI | NAND_HAS_DIO | NAND_HAS_QIO | NAND_HAS_DDR
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25N_KV,
        NAND_HAS_SPI | NAND_HAS_DIO | NAND_HAS_QIO
    }, {
        JEDEC_MANUFACTURER_WINBOND,
        JEDEC_DEVICE_WINBOND_W25N_KW,
        NAND_HAS_SPI | NAND_HAS_DIO | NAND_HAS_QIO
    }
};
/*----------------------------------------------------------------------------*/
static const struct NandCapabilityEntry *findCapabilityEntry(
    const struct JedecInfo *info)
{
  for (size_t index = 0; index < ARRAY_SIZE(nandCapabilityMap); ++index)
  {
    if (nandCapabilityMap[index].manufacturer == info->manufacturer
        && nandCapabilityMap[index].type == info->type)
    {
      return &nandCapabilityMap[index];
    }
  }

  return NULL;
}
/*----------------------------------------------------------------------------*/
uint16_t nandGetCapabilitiesByJedecInfo(const struct JedecInfo *info)
{
  const struct NandCapabilityEntry * const entry = findCapabilityEntry(info);
  return entry != NULL ? entry->capabilities : 0;
}
