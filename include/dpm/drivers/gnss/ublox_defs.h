/*
 * drivers/gnss/ublox_defs.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_GNSS_UBLOX_DEFS_H_
#define DPM_DRIVERS_GNSS_UBLOX_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <stdint.h>
/*----------------------------------------------------------------------------*/
#define UBLOX_TYPE_PACK(group, id) (((id) << 8) | (group))
/*----------------------------------------------------------------------------*/
enum UbloxMessageClass
{
  UBX_TIM = 0x0D
};

enum UbloxMessageId
{
  UBX_TIM_TP = 0x01
};
/*----------------------------------------------------------------------------*/
struct UbxTimTpPacket {
  uint32_t towMS;
  uint32_t towSubMS;
  int32_t qErr;
  uint16_t week;
  uint8_t flags;
  uint8_t refInfo;
} __attribute__((packed));
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_GNSS_UBLOX_DEFS_H_ */
