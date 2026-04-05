/*
 * sensors/thermistor_ntc.c
 * Copyright (C) 2026 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <dpm/sensors/thermistor_ntc.h>
#include <limits.h>
/*----------------------------------------------------------------------------*/
size_t findBinIndex(const int16_t *, size_t, int32_t);
/*----------------------------------------------------------------------------*/
size_t findBinIndex(const int16_t *table, size_t size, int32_t temperature)
{
  size_t left = 0;
  size_t right = size - 1;

  if (temperature < table[right])
    return 0;
  if (temperature > table[left])
    return size;

  while ((right - left) > 1)
  {
    const size_t medium = (left + right) >> 1;

    if (temperature > table[medium])
      right = medium;
    else
      left = medium;
  }

  return right;
}
/*----------------------------------------------------------------------------*/
int32_t ntcRawToTemperature(const int16_t *table, size_t size, uint16_t value)
{
  const int32_t step = (UINT16_MAX + 1) / (size - 1);
  const uint32_t binIndex = (uint32_t)((int32_t)value / step);

  const int32_t currentBinOutput = table[binIndex];
  const int32_t currentBinValue = (int32_t)binIndex * step;
  const int32_t slope = table[binIndex + 1] - currentBinOutput;

  return currentBinOutput
      + (((int32_t)value - currentBinValue) * slope - step / 2) / step;
}
/*----------------------------------------------------------------------------*/
uint16_t ntcTemperatureToRaw(const int16_t *table, size_t size,
    int32_t temperature)
{
  const int32_t step = (UINT16_MAX + 1) / (size - 1);
  const uint32_t binIndex = (uint32_t)findBinIndex(table, size, temperature);

  if (binIndex == 0)
    return UINT16_MAX;
  if (binIndex == size)
    return 0;

  const int32_t previousBinOutput = table[binIndex - 1];
  const int32_t previousRawOutput = (int32_t)((binIndex - 1) * step);
  const int32_t slope = table[binIndex] - previousBinOutput;
  const int32_t offset = temperature - previousBinOutput;
  const int32_t raw = previousRawOutput + (offset * step + slope / 2) / slope;

  return (uint16_t)raw;
}
