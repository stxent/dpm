/*
 * memory/w25_defs.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_W25_DEFS_H_
#define DPM_MEMORY_W25_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <xcore/bits.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
#define JEDEC_MANUFACTURER_WINBOND        0xEF

#define JEDEC_DEVICE_IN_IQ_JQ             0x40
#define JEDEC_DEVICE_IM_JM                0x70

#define JEDEC_CAPACITY_W25Q016            0x15  /* 16 Mbit */
#define JEDEC_CAPACITY_W25Q032            0x16  /* 32 Mbit */
#define JEDEC_CAPACITY_W25Q064            0x17  /* 64 Mbit */
#define JEDEC_CAPACITY_W25Q128            0x18  /* 128 Mbit */
#define JEDEC_CAPACITY_W25Q256            0x19  /* 256 Mbit */
#define JEDEC_CAPACITY_W25Q512            0x20  /* 512 Mbit */
#define JEDEC_CAPACITY_W25Q01             0x21  /* 1 Gbit */
#define JEDEC_CAPACITY_W25Q02             0x22  /* 2 Gbit */
/*----------------------------------------------------------------------------*/
#define CMD_WRITE_STATUS_REGISTER_1       0x01
#define CMD_WRITE_DISABLE                 0x04
#define CMD_READ_STATUS_REGISTER_1        0x05
#define CMD_WRITE_ENABLE                  0x06
#define CMD_WRITE_STATUS_REGISTER_3       0x11
#define CMD_READ_STATUS_REGISTER_3        0x15
#define CMD_WRITE_STATUS_REGISTER_2       0x31
#define CMD_READ_STATUS_REGISTER_2        0x35
#define CMD_WRITE_ENABLE_VOLATILE         0x50
#define CMD_ENTER_4BYTE_ADDRESS_MODE      0xB7
#define CMD_READ_EXTENDED_ADDRESS         0xC8
#define CMD_EXIT_4BYTE_ADDRESS_MODE       0xE9

#define CMD_READ_DATA                     0x03
#define CMD_FAST_READ                     0x0B
#define CMD_FAST_READ_4BYTE               0x0C
#define CMD_FAST_READ_DTR                 0x0D
#define CMD_BURST_READ_WITH_WRAP_DTR      0x0E
#define CMD_READ_DATA_4BYTE               0x13
#define CMD_FAST_READ_DUAL_OUTPUT         0x3B
#define CMD_FAST_READ_DUAL_OUTPUT_4BYTE   0x3C
#define CMD_FAST_READ_QUAD_OUTPUT         0x6B
#define CMD_FAST_READ_QUAD_OUTPUT_4BYTE   0x6C
#define CMD_SET_BURST_WITH_WRAP           0x77
#define CMD_FAST_READ_DUAL_IO             0xBB
#define CMD_FAST_READ_DUAL_IO_4BYTE       0xBC
#define CMD_FAST_READ_DUAL_IO_DTR         0xBD
#define CMD_FAST_READ_QUAD_IO             0xEB
#define CMD_FAST_READ_QUAD_IO_4BYTE       0xEC
#define CMD_FAST_READ_QUAD_IO_DTR         0xED

#define CMD_PAGE_PROGRAM                  0x02
#define CMD_PAGE_PROGRAM_4BYTE            0x12
#define CMD_PAGE_PROGRAM_QUAD_INPUT       0x32
#define CMD_PAGE_PROGRAM_QUAD_INPUT_4BYTE 0x34

#define CMD_SECTOR_ERASE                  0x20
#define CMD_SECTOR_ERASE_4BYTE            0x21
#define CMD_BLOCK_ERASE_32KB              0x52
#define CMD_CHIP_ERASE                    0x60
#define CMD_ERASE_PROGRAM_SUSPEND         0x75
#define CMD_ERASE_PROGRAM_RESUME          0x7A
#define CMD_BLOCK_ERASE_64KB              0xD8
#define CMD_BLOCK_ERASE_64KB_4BYTE        0xDC

#define CMD_READ_UNIQUE_ID                0x4B
#define CMD_READ_SFDP                     0x5A
#define CMD_READ_DEVICE_ID                0x90
#define CMD_READ_DEVICE_ID_DUAL_IO        0x92
#define CMD_READ_DEVICE_ID_QUAD_IO        0x94
#define CMD_READ_JEDEC_ID                 0x9F

#define CMD_SINGLE_BLOCK_SECTOR_LOCK      0x36
#define CMD_SINGLE_BLOCK_SECTOR_UNLOCK    0x39
#define CMD_READ_BLOCK_SECTOR_LOCK        0x3D
#define CMD_PROGRAM_SECURITY_REGISTERS    0x42
#define CMD_ERASE_SECURITY_REGISTERS      0x44
#define CMD_READ_SECURITY_REGISTERS       0x48
#define CMD_GLOBAL_BLOCK_SECTOR_LOCK      0x7E
#define CMD_GLOBAL_BLOCK_SECTOR_UNLOCK    0x98

#define CMD_ADDRESS_4BYTE_ENTER           0xB7
#define CMD_ADDRESS_4BYTE_EXIT            0xE9
#define CMD_ENTER_QPI                     0x38
#define CMD_EXIT_QPI                      0xFF
#define CMD_POWER_DOWN                    0xB9
#define CMD_POWER_DOWN_RELEASE            0xAB
#define CMD_RESET_DEVICE                  0x99
#define CMD_RESET_ENABLE                  0x66
#define CMD_SET_READ_PARAMETERS           0xC0
/*----------------------------------------------------------------------------*/
#define MEMORY_PAGE_SIZE                  256
#define MEMORY_SECTOR_4KB_SIZE            4096
#define MEMORY_BLOCK_32KB_SIZE            32768
#define MEMORY_BLOCK_64KB_SIZE            65536
/*------------------Status Register 1-----------------------------------------*/
#define SR1_BUSY  BIT(0)
#define SR1_WEL   BIT(1)
#define SR1_BP0   BIT(2)
#define SR1_BP1   BIT(3)
#define SR1_BP2   BIT(4)
#define SR1_BP3   BIT(5)
#define SR1_TB    BIT(6)
#define SR1_SRP   BIT(7)
/*------------------Status Register 2-----------------------------------------*/
#define SR2_SRL   BIT(0)
#define SR2_QE    BIT(1)
#define SR2_LB1   BIT(3)
#define SR2_LB2   BIT(4)
#define SR2_LB3   BIT(5)
#define SR2_CMP   BIT(6)
#define SR2_SUS   BIT(7)
/*------------------Status Register 3-----------------------------------------*/
#define SR3_ADS   BIT(0)
#define SR3_ADP   BIT(1)
#define SR3_WPS   BIT(2)
#define SR3_DRV0  BIT(5)
#define SR3_DRV1  BIT(6)

#define SR3_DRV_MASK        BIT_FIELD(MASK(2), 5)
#define SR3_DRV(value)      BIT_FIELD((value), 5)
#define SR3_DRV_VALUE(reg)  FIELD_VALUE((reg), SR3_DRV_MASK, 5)
/*------------------XIP mode--------------------------------------------------*/
/* Bits M5-4 must be set to 0b10 */
#define XIP_MODE_ENTER      0xEF
/* Bits M5-4 must not be equal to 0b10 */
#define XIP_MODE_EXIT       0xFF
/*----------------------------------------------------------------------------*/
struct [[gnu::packed]] JedecInfo
{
  uint8_t manufacturer;
  uint8_t type;
  uint8_t capacity;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_W25_DEFS_H_ */
