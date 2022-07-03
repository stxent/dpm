/*
 * sensors/xpt2046_defs.h
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_XPT2046_DEFS_H_
#define DPM_SENSORS_XPT2046_DEFS_H_
/*----------------------------------------------------------------------------*/
#define CTRL_ADC_ON   BIT(0)
#define CTRL_REF_ON   BIT(1)
#define CTRL_DFR      0
#define CTRL_SER      BIT(2)
#define CTRL_HI_Y     BIT_FIELD(0x09, 4)
#define CTRL_Z1_POS   BIT_FIELD(0x0B, 4)
#define CTRL_Z2_POS   BIT_FIELD(0x0C, 4)
#define CTRL_HI_X     BIT_FIELD(0x0D, 4)
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_XPT2046_DEFS_H_ */
