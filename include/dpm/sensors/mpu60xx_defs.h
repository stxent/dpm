/*
 * sensors/mpu60xx_defs.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_MPU60XX_DEFS_H_
#define DPM_SENSORS_MPU60XX_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <dpm/sensors/sensor.h>
#include <xcore/bits.h>
/*----------------------------------------------------------------------------*/
enum
{
  FLAG_RESET         = 0x0001,
  FLAG_READY         = 0x0002,
  FLAG_EVENT         = 0x0004,
  FLAG_ACCEL_LOOP    = 0x0008,
  FLAG_ACCEL_SAMPLE  = 0x0010,
  FLAG_GYRO_LOOP     = 0x0020,
  FLAG_GYRO_SAMPLE   = 0x0040,
  FLAG_THERMO_LOOP   = 0x0080,
  FLAG_THERMO_SAMPLE = 0x0100,

  FLAG_LOOP          = FLAG_ACCEL_LOOP | FLAG_GYRO_LOOP | FLAG_THERMO_LOOP,
  FLAG_SAMPLE        = FLAG_ACCEL_SAMPLE | FLAG_GYRO_SAMPLE | FLAG_THERMO_SAMPLE
};

enum
{
  REG_SMPLRT_DIV          = 0x19,
  REG_CONFIG              = 0x1A,
  REG_GYRO_CONFIG         = 0x1B,
  REG_ACCEL_CONFIG        = 0x1C,
  REG_FIFO_EN             = 0x23,
  REG_INT_PIN_CFG         = 0x37,
  REG_INT_ENABLE          = 0x38,
  REG_INT_STATUS          = 0x3A,
  REG_ACCEL_XOUT_H        = 0x3B,
  REG_ACCEL_XOUT_L        = 0x3C,
  REG_ACCEL_YOUT_H        = 0x3D,
  REG_ACCEL_YOUT_L        = 0x3E,
  REG_ACCEL_ZOUT_H        = 0x3F,
  REG_ACCEL_ZOUT_L        = 0x40,
  REG_TEMP_OUT_H          = 0x41,
  REG_TEMP_OUT_L          = 0x42,
  REG_GYRO_XOUT_H         = 0x43,
  REG_GYRO_XOUT_L         = 0x44,
  REG_GYRO_YOUT_H         = 0x45,
  REG_GYRO_YOUT_L         = 0x46,
  REG_GYRO_ZOUT_H         = 0x47,
  REG_GYRO_ZOUT_L         = 0x48,
  REG_SIGNAL_PATH_RESET   = 0x68,
  REG_USER_CTRL           = 0x6A,
  REG_PWR_MGMT_1          = 0x6B,
  REG_PWR_MGMT_2          = 0x6C,
  REG_FIFO_COUNTH         = 0x72,
  REG_FIFO_COUNTL         = 0x73,
  REG_FIFO_R_W            = 0x74,
  REG_WHO_AM_I            = 0x75
};
/*------------------Configuration register------------------------------------*/
enum
{
  /** Accel sample rate 1 kHz, gyro sample rate 8 kHz */
  DLPF_CFG_ACCEL_260_GYRO_256 = 0,
  /** 1 kHz accel and gyro sample rates */
  DLPF_CFG_ACCEL_184_GYRO_188,
  /** 1 kHz accel and gyro sample rates */
  DLPF_CFG_ACCEL_94_GYRO_98,
  /** 1 kHz accel and gyro sample rates */
  DLPF_CFG_ACCEL_44_GYRO_42,
  /** 1 kHz accel and gyro sample rates */
  DLPF_CFG_ACCEL_21_GYRO_20,
  /** 1 kHz accel and gyro sample rates */
  DLPF_CFG_ACCEL_10_GYRO_10,
  /** 1 kHz accel and gyro sample rates */
  DLPF_CFG_ACCEL_5_GYRO_5
};

enum
{
  EXT_SYNC_SET_DISABLED = 0,
  EXT_SYNC_SET_TEMP_OUT_L,
  EXT_SYNC_SET_GYRO_XOUT_L,
  EXT_SYNC_SET_GYRO_YOUT_L,
  EXT_SYNC_SET_GYRO_ZOUT_L,
  EXT_SYNC_SET_ACCEL_XOUT_L,
  EXT_SYNC_SET_ACCEL_YOUT_L,
  EXT_SYNC_SET_ACCEL_ZOUT_L
};

#define CONFIG_DLPF_CFG(value)          BIT_FIELD((value), 0)
#define CONFIG_DLPF_CFG_MASK            BIT_FIELD(MASK(3), 0)
#define CONFIG_DLPF_CFG_VALUE(reg) \
    FIELD_VALUE((reg), CONFIG_DLPF_CFG_MASK, 0)

#define CONFIG_EXT_SYNC_SET(value)      BIT_FIELD((value), 3)
#define CONFIG_EXT_SYNC_SET_MASK        BIT_FIELD(MASK(3), 3)
#define CONFIG_EXT_SYNC_SET_VALUE(reg) \
    FIELD_VALUE((reg), CONFIG_EXT_SYNC_SET_MASK, 3)
/*------------------Gyroscope Configuration register--------------------------*/
enum
{
  GYRO_FS_SEL_250 = 0,
  GYRO_FS_SEL_500,
  GYRO_FS_SEL_1000,
  GYRO_FS_SEL_2000
};

#define GYRO_CONFIG_FS_SEL(value)       BIT_FIELD((value), 3)
#define GYRO_CONFIG_FS_SEL_MASK         BIT_FIELD(MASK(2), 3)
#define GYRO_CONFIG_FS_SEL_VALUE(reg) \
    FIELD_VALUE((reg), GYRO_CONFIG_FS_SEL_MASK, 3)

#define GYRO_CONFIG_ZG_ST               BIT(5)
#define GYRO_CONFIG_YG_ST               BIT(6)
#define GYRO_CONFIG_XG_ST               BIT(7)
/*------------------Accelerometer Configuration register----------------------*/
enum
{
  ACCEL_FS_SEL_2 = 0,
  ACCEL_FS_SEL_4,
  ACCEL_FS_SEL_8,
  ACCEL_FS_SEL_16
};

#define ACCEL_CONFIG_FS_SEL(value)      BIT_FIELD((value), 3)
#define ACCEL_CONFIG_FS_SEL_MASK        BIT_FIELD(MASK(2), 3)
#define ACCEL_CONFIG_FS_SEL_VALUE(reg) \
    FIELD_VALUE((reg), ACCEL_CONFIG_FS_SEL_MASK, 3)

#define ACCEL_CONFIG_ZA_ST              BIT(5)
#define ACCEL_CONFIG_YA_ST              BIT(6)
#define ACCEL_CONFIG_XA_ST              BIT(7)
/*------------------FIFO Enable register--------------------------------------*/
#define FIFO_EN_SLV0_FIFO_EN            BIT(0)
#define FIFO_EN_SLV1_FIFO_EN            BIT(1)
#define FIFO_EN_SLV2_FIFO_EN            BIT(2)
#define FIFO_EN_ACCEL_FIFO_EN           BIT(3)
#define FIFO_EN_ZG_FIFO_EN              BIT(4)
#define FIFO_EN_YG_FIFO_EN              BIT(5)
#define FIFO_EN_XG_FIFO_EN              BIT(6)
#define FIFO_EN_TEMP_FIFO_EN            BIT(7)
/*------------------INT Pin/Bypass Enable Configuration register--------------*/
#define INT_PIN_CFG_I2C_BYPASS_EN       BIT(1)
#define INT_PIN_CFG_FSYNC_INT_EN        BIT(2)
#define INT_PIN_CFG_FSYNC_INT_LEVEL     BIT(3)
#define INT_PIN_CFG_INT_RD_CLEAR        BIT(4)
#define INT_PIN_CFG_LATCH_INT_EN        BIT(5)
#define INT_PIN_CFG_INT_OPEN            BIT(6)
#define INT_PIN_CFG_INT_LEVEL           BIT(7)
/*------------------Interrupt Enable register---------------------------------*/
#define INT_ENABLE_DATA_RDY_EN          BIT(0)
#define INT_ENABLE_I2C_MST_INT_EN       BIT(3)
#define INT_ENABLE_FIFO_OFLOW_EN        BIT(4)
/*------------------Interrupt Status register---------------------------------*/
#define INT_STATUS_DATA_RDY_INT         BIT(0)
#define INT_STATUS_I2C_MST_INT          BIT(3)
#define INT_STATUS_FIFO_OFLOW_INT       BIT(4)
/*------------------Signal Path Reset register--------------------------------*/
#define SIGNAL_PATH_RESET_TEMP_RESET    BIT(0)
#define SIGNAL_PATH_RESET_ACCEL_RESET   BIT(1)
#define SIGNAL_PATH_RESET_GYRO_RESET    BIT(2)
/*------------------User Control register-------------------------------------*/
#define USER_CTRL_SIG_COND_RESET        BIT(0)
#define USER_CTRL_I2C_MST_RESET         BIT(1)
#define USER_CTRL_FIFO_RESET            BIT(2)
#define USER_CTRL_I2C_IF_DIS            BIT(4)
#define USER_CTRL_I2C_MST_EN            BIT(5)
#define USER_CTRL_FIFO_EN               BIT(6)
/*------------------Power Management 1 register-------------------------------*/
enum
{
  CLKSEL_INT,
  CLKSEL_XG,
  CLKSEL_YG,
  CLKSEL_ZG,
  CLKSEL_EXT_32K768,
  CLKSEL_EXT_19M2,
  CLKSEL_STOP
};

#define PWR_MGMT_1_CLKSEL(value)        BIT_FIELD((value), 0)
#define PWR_MGMT_1_CLKSEL_MASK          BIT_FIELD(MASK(3), 0)
#define PWR_MGMT_1_CLKSEL_VALUE(reg) \
    FIELD_VALUE((reg), PWR_MGMT_1_CLKSEL_MASK, 0)

#define PWR_MGMT_1_TEMP_DIS             BIT(3)
#define PWR_MGMT_1_CYCLE                BIT(5)
#define PWR_MGMT_1_SLEEP                BIT(6)
#define PWR_MGMT_1_DEVICE_RESET         BIT(7)
/*------------------Power Management 2 register-------------------------------*/
#define PWR_MGMT_2_STBY_ZG              BIT(0)
#define PWR_MGMT_2_STBY_YG              BIT(1)
#define PWR_MGMT_2_STBY_XG              BIT(2)
#define PWR_MGMT_2_STBY_ZA              BIT(3)
#define PWR_MGMT_2_STBY_YA              BIT(4)
#define PWR_MGMT_2_STBY_XA              BIT(5)

#define PWR_MGMT_2_LP_WAKE_CTRL(value)  BIT_FIELD((value), 6)
#define PWR_MGMT_2_LP_WAKE_CTRL_MASK    BIT_FIELD(MASK(2), 6)
#define PWR_MGMT_2_LP_WAKE_CTRL_VALUE(reg) \
    FIELD_VALUE((reg), PWR_MGMT_2_LP_WAKE_CTRL_MASK, 6)
/*----------------------------------------------------------------------------*/
#define WHO_AM_I_MPU60XX_VALUE          0x68
/*----------------------------------------------------------------------------*/
enum SensorStatus mpu60xxGetStatus(const struct MPU60XX *);
void mpu60xxReset(struct MPU60XX *);
void mpu60xxSample(struct MPU60XX *);
void mpu60xxStart(struct MPU60XX *);
void mpu60xxStop(struct MPU60XX *);
bool mpu60xxUpdate(struct MPU60XX *);
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_MPU60XX_DEFS_H_ */
