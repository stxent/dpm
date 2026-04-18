/*
 * sensors/thermistor_ntc.c
 * Copyright (C) 2026 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <dpm/sensors/thermistor_ntc.h>
/*----------------------------------------------------------------------------*/
size_t ntcFindBinIndex(size_t size, const int16_t table[static size],
    int32_t temperature)
{
  size_t left = 0;
  size_t right = size - 1;

  if (temperature < table[right])
    return 0;
  if (temperature > table[left])
    return SIZE_MAX;

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
