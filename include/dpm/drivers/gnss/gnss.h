/*
 * drivers/gnss/gnss.h
 * Copyright (C) 2021 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_DRIVERS_GNSS_GNSS_H_
#define DPM_DRIVERS_GNSS_GNSS_H_
/*----------------------------------------------------------------------------*/
#include <stdint.h>
/*----------------------------------------------------------------------------*/
enum FixType
{
  FIX_NONE,
  FIX_DEAD_RECKONING,
  FIX_2D,
  FIX_3D,
  FIX_3D_CORRECTED
} __attribute__((packed));

struct SatelliteInfo
{
  uint8_t gps;
  uint8_t glonass;
  uint8_t beidou;
  uint8_t galileo;
  uint8_t sbas;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_GNSS_GNSS_H_ */