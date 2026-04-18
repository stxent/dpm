/*
 * sensors/thermistor_ntc.h
 * Copyright (C) 2026 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_SENSORS_THERMISTOR_NTC_H_
#define DPM_SENSORS_THERMISTOR_NTC_H_
/*----------------------------------------------------------------------------*/
#include <xcore/helpers.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

size_t ntcFindBinIndex(size_t size, const int16_t [static size], int32_t);

END_DECLS
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

static inline int32_t ntcRawToTemperature(size_t size,
    const int16_t table[static size], uint16_t value)
{
  const int32_t step = (UINT16_MAX + 1) / (size - 1);
  const size_t binIndex = (size_t)((int32_t)value / step);

  const int32_t currentBinOutput = table[binIndex];
  const int32_t slope = table[binIndex + 1] - currentBinOutput;
  const int32_t offset = (int32_t)value - (int32_t)binIndex * step;

  return currentBinOutput + (offset * slope - (step >> 1)) / step;
}

static inline uint16_t ntcTemperatureToRaw(size_t size,
    const int16_t table[static size], int32_t temperature)
{
  const int32_t step = (UINT16_MAX + 1) / (size - 1);
  const size_t binIndex = ntcFindBinIndex(size, table, temperature);

  if (binIndex == 0)
    return UINT16_MAX;
  if (binIndex == SIZE_MAX)
    return 0;

  const int32_t previousBinOutput = table[binIndex - 1];
  const int32_t previousRawOutput = ((int32_t)binIndex - 1) * step;
  const int32_t slope = table[binIndex] - previousBinOutput;
  const int32_t offset = temperature - previousBinOutput;

  return (uint16_t)(previousRawOutput + (offset * step + (slope >> 1)) / slope);
}

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_THERMISTOR_NTC_H_ */
