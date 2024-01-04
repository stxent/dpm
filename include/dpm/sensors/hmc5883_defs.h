/*
 * sensors/hmc5883_defs.h
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_HMC5883_DEFS_H_
#define DPM_SENSORS_HMC5883_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <xcore/bits.h>
/*----------------------------------------------------------------------------*/
enum
{
  CAL_DISABLED,
  CAL_NEG_OFFSET,
  CAL_POS_OFFSET
};

enum
{
  FLAG_RESET    = 0x01,
  FLAG_READY    = 0x02,
  FLAG_EVENT    = 0x04,
  FLAG_LOOP     = 0x08,
  FLAG_SAMPLE   = 0x10,
  FLAG_SUSPEND  = 0x20
};

enum
{
  REG_CONFIG_A    = 0,
  REG_CONFIG_B    = 1,
  REG_MODE        = 2,
  REG_DATA_X_MSB  = 3,
  REG_DATA_X_LSB  = 4,
  REG_DATA_Z_MSB  = 5,
  REG_DATA_Z_LSB  = 6,
  REG_DATA_Y_MSB  = 7,
  REG_DATA_Y_LSB  = 8,
  REG_STATUS      = 9,
  REG_ID_A        = 10,
  REG_ID_B        = 11,
  REG_ID_C        = 12
};
/*------------------Configuration Register A----------------------------------*/
enum
{
  DO_0_75_HZ = 0,
  DO_1_5_HZ  = 1,
  DO_3_HZ    = 2,
  DO_7_5_HZ  = 3,
  DO_15_HZ   = 4,
  DO_30_HZ   = 5,
  DO_75_HZ   = 6
};

enum
{
  MA_1_SAMPLE   = 0,
  MA_2_SAMPLES  = 1,
  MA_4_SAMPLES  = 2,
  MA_8_SAMPLES  = 3
};

enum
{
  MS_NORMAL         = 0,
  MS_POSITIVE_BIAS  = 1,
  MS_NEGATIVE_BIAS  = 2
};

#define CONFIG_A_MS(value)              BIT_FIELD((value), 0)
#define CONFIG_A_MS_MASK                BIT_FIELD(MASK(2), 0)
#define CONFIG_A_MS_VALUE(reg)          FIELD_VALUE((reg), CONFIG_A_MS_MASK, 0)

#define CONFIG_A_DO(value)              BIT_FIELD((value), 2)
#define CONFIG_A_DO_MASK                BIT_FIELD(MASK(3), 2)
#define CONFIG_A_DO_VALUE(reg)          FIELD_VALUE((reg), CONFIG_A_DO_MASK, 2)

#define CONFIG_A_MA(value)              BIT_FIELD((value), 5)
#define CONFIG_A_MA_MASK                BIT_FIELD(MASK(2), 5)
#define CONFIG_A_MA_VALUE(reg)          FIELD_VALUE((reg), CONFIG_A_MA_MASK, 5)
/*------------------Configuration Register B----------------------------------*/
enum
{
  GN_0_88_GA  = 0,
  GN_1_3_GA   = 1,
  GN_1_9_GA   = 2,
  GN_2_5_GA   = 3,
  GN_4_0_GA   = 4,
  GN_4_7_GA   = 5,
  GN_5_6_GA   = 6,
  GN_8_1_GA   = 7
};

#define CONFIG_B_GN(value)              BIT_FIELD((value), 5)
#define CONFIG_B_GN_MASK                BIT_FIELD(MASK(3), 5)
#define CONFIG_B_GN_VALUE(reg)          FIELD_VALUE((reg), CONFIG_B_GN_MASK, 5)
/*------------------Mode Register---------------------------------------------*/
enum
{
  MD_CONTINUOUS = 0,
  MD_SINGLE     = 1,
  MD_IDLE       = 2
};

#define MODE_MD(value)                  BIT_FIELD((value), 0)
#define MODE_MD_MASK                    BIT_FIELD(MASK(2), 0)
#define MODE_MD_VALUE(reg)              FIELD_VALUE((reg), MODE_MD_MASK, 0)

#define MODE_HS                         BIT(7)
/*------------------Status Register-------------------------------------------*/
#define STATUS_RDY                      BIT(0)
#define STATUS_LOCK                     BIT(1)
/*------------------Identification Registers----------------------------------*/
#define ID_A_HMC5883_VALUE              'H'
#define ID_B_HMC5883_VALUE              '4'
#define ID_C_HMC5883_VALUE              '3'
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_HMC5883_DEFS_H_ */
