/*
 * sensors/sht2x_defs.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_SHT2X_DEFS_H_
#define DPM_SENSORS_SHT2X_DEFS_H_
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

enum Command
{
  CMD_TRIGGER_T_HOLD  = 0xE3,
  CMD_TRIGGER_RH_HOLD = 0xE5,
  CMD_TRIGGER_T       = 0xF3,
  CMD_TRIGGER_RH      = 0xF5,
  CMD_WRITE_USER_REG  = 0xE6,
  CMD_READ_USER_REG   = 0xE7,
  CMD_SOFT_RESET      = 0xFE
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_SHT2X_DEFS_H_ */
