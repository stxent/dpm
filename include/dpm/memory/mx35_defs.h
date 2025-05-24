/*
 * memory/mx35_defs.h
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_MX35_DEFS_H_
#define DPM_MEMORY_MX35_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <xcore/bits.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
#define CMD_WRITE_DISABLE               0x04
#define CMD_WRITE_ENABLE                0x06
#define CMD_READ_ID                     0x9F
#define CMD_RESET                       0xFF

#define CMD_GET_FEATURE                 0x0F
#define CMD_SET_FEATURE                 0x1F
#define CMD_PAGE_READ                   0x13
#define CMD_PAGE_READ_CACHE_RANDOM      0x30
#define CMD_PAGE_READ_CACHE_SEQUENTIAL  0x31
#define CMD_PAGE_READ_CACHE_END         0x3F
#define CMD_READ_FROM_CACHE             0x03
#define CMD_READ_FROM_CACHE_X2          0x3B
#define CMD_READ_FROM_CACHE_X4          0x6B
#define CMD_READ_FROM_CACHE_DUAL_IO     0xBB
#define CMD_READ_FROM_CACHE_QUAD_IO     0xEB

#define CMD_PROGRAM_LOAD                0x02
#define CMD_PROGRAM_EXECUTE             0x10
#define CMD_PROGRAM_LOAD_X4             0x32
#define CMD_PROGRAM_LOAD_RANDOM_DATA_X4 0x34
#define CMD_PROGRAM_LOAD_RANDOM_DATA    0x84
#define CMD_BLOCK_ERASE                 0xD8
/*----------------------------------------------------------------------------*/
enum
{
  FEATURE_BP     = 0xA0,
  FEATURE_CFG    = 0xB0,
  FEATURE_STATUS = 0xC0
};

#define MEMORY_PAGE_2K_COLUMN_SIZE      12
#define MEMORY_PAGE_2K_SIZE             2112
#define MEMORY_PAGE_2K_ECC_SIZE         2176
#define MEMORY_PAGE_4K_COLUMN_SIZE      13
#define MEMORY_PAGE_4K_SIZE             4224
#define MEMORY_PAGE_4K_ECC_SIZE         4352
#define MEMORY_PAGES_PER_BLOCK          64
/*------------------Configuration Feature Register----------------------------*/
#define FR_CFG_QE                       BIT(0)
#define FR_CFG_CONTINUOUS               BIT(2)
#define FR_CFG_ECC_ENABLE               BIT(4)
#define FR_CFG_OTP_ENABLE               BIT(6)
#define FR_CFG_OTP_PROTECT              BIT(7)
/*------------------Status Feature Register-----------------------------------*/
#define FR_STATUS_OIP                   BIT(0)
#define FR_STATUS_WEL                   BIT(1)
#define FR_STATUS_E_FAIL                BIT(2)
#define FR_STATUS_P_FAIL                BIT(3)
#define FR_STATUS_CRBSY                 BIT(6)
/*------------------Block Protection Feature Register-------------------------*/
#define FR_BP_SP                        BIT(0)
#define FR_BP_COMPLEMENTARY             BIT(1)
#define FR_BP_INVERT                    BIT(2)
#define FR_BP_BP0                       BIT(3)
#define FR_BP_BP1                       BIT(4)
#define FR_BP_BP2                       BIT(5)
#define FR_BP_BPRWD                     BIT(7)
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_MX35_DEFS_H_ */
