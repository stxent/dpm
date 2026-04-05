/*
 * sensors/thermistor_ntc.h
 * Copyright (C) 2026 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_SENSORS_THERMISTOR_NTC_H_
#define DPM_SENSORS_THERMISTOR_NTC_H_
/*----------------------------------------------------------------------------*/
#include <xcore/helpers.h>
#include <stddef.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

int32_t ntcRawToTemperature(const int16_t *, size_t, uint16_t);
uint16_t ntcTemperatureToRaw(const int16_t *, size_t, int32_t);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_THERMISTOR_NTC_H_ */
