/*
 * sensors/ms56xx_defs.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_MS56XX_DEFS_H_
#define DPM_SENSORS_MS56XX_DEFS_H_
/*----------------------------------------------------------------------------*/
enum
{
  FLAG_RESET         = 0x01,
  FLAG_READY         = 0x02,
  FLAG_LOOP          = 0x04,
  FLAG_SAMPLE        = 0x08,
  FLAG_THERMO_LOOP   = 0x10,
  FLAG_THERMO_SAMPLE = 0x20
};

enum
{
  PROM_SENS     = 1,
  PROM_OFF      = 2,
  PROM_TCS      = 3,
  PROM_TCO      = 4,
  PROM_TREF     = 5,
  PROM_TEMPSENS = 6
};

enum OversamplingMode
{
  OSR_256,
  OSR_512,
  OSR_1024,
  OSR_2048,
  OSR_4096
};
/*----------------------------------------------------------------------------*/
#define CMD_READ_ADC          0x00
#define CMD_RESET             0x1E
#define CMD_CONVERT_D1(osr)   (0x40 | ((osr) << 1))
#define CMD_CONVERT_D2(osr)   (0x50 | ((osr) << 1))
#define CMD_READ_PROM(index)  (0xA0 | ((index) << 1))
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_MS56XX_DEFS_H_ */
