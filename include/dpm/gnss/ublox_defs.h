/*
 * gnss/ublox_defs.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_GNSS_UBLOX_DEFS_H_
#define DPM_GNSS_UBLOX_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <stdint.h>
/*----------------------------------------------------------------------------*/
#define UBLOX_MESSAGE_LENGTH            1024
#define UBLOX_MESSAGE_OVERHEAD          8
#define UBLOX_TYPE_PACK(group, id)      (((id) << 8) | (group))
#define UBLOX_TYPE_GROUP(type)          (type & 0xFF)
#define UBLOX_TYPE_ID(type)             ((type >> 8) & 0xFF)
/*----------------------------------------------------------------------------*/
enum UbloxMessageClass
{
  UBX_NAV = 0x01,
  UBX_RXM = 0x02,
  UBX_INF = 0x04,
  UBX_ACK = 0x05,
  UBX_CFG = 0x06,
  UBX_UPD = 0x09,
  UBX_MON = 0x0A,
  UBX_AID = 0x0B,
  UBX_TIM = 0x0D,
  UBX_MGA = 0x13,
  UBX_LOG = 0x21
};

enum UbloxMessageId
{
  UBX_NAV_POSLLH    = 0x02,
  UBX_NAV_STATUS    = 0x03,
  UBX_NAV_SOL       = 0x06,
  UBX_NAV_PVT       = 0x07,
  UBX_NAV_VELNED    = 0x12,
  UBX_NAV_TIMEGPS   = 0x20,
  UBX_NAV_SVINFO    = 0x30,
  UBX_NAV_SAT       = 0x35,
  UBX_NAV_RELPOSNED = 0x3C,

  UBX_ACK_NAK       = 0x00,
  UBX_ACK_ACK       = 0x01,

  UBX_CFG_PRT       = 0x00,
  UBX_CFG_MSG       = 0x01,
  UBX_CFG_RST       = 0x04,
  UBX_CFG_RATE      = 0x08,
  UBX_CFG_ODO       = 0x1E,
  UBX_CFG_NAV5      = 0x24,
  UBX_CFG_TP5       = 0x31,
  UBX_CFG_GNSS      = 0x3E,
  UBX_CFG_DGNSS     = 0x70,

  UBX_CFG_VALGET    = 0x8B,
  UBX_CFG_VALSET    = 0x8A,
  UBX_CFG_VALDEL    = 0x8C,

  UBX_TIM_TP        = 0x01,

  UBX_RXM_RAW       = 0x10,
  UBX_RXM_RAWX      = 0x15,

  UBX_MON_VER       = 0x04
};

enum UbloxSystemId
{
  UBX_SYSTEM_GPS     = 0,
  UBX_SYSTEM_SBAS    = 1,
  UBX_SYSTEM_GALILEO = 2,
  UBX_SYSTEM_BEIDOU  = 3,
  UBX_SYSTEM_IMES    = 4,
  UBX_SYSTEM_QZSS    = 5,
  UBX_SYSTEM_GLONASS = 6
};
/*----------------------------------------------------------------------------*/
struct [[gnu::packed]] UbxAckAckPacket {
	uint8_t clsID;
	uint8_t msgID;
};

struct [[gnu::packed]] UbxAckNakPacket {
	uint8_t clsID;
	uint8_t msgID;
};

struct [[gnu::packed]] UbxCfgMsgPacket {
	uint8_t msgClass;
	uint8_t msgID;
	uint8_t rate;
};

struct [[gnu::packed]] UbxCfgPrtPacket {
	uint8_t portID;
	uint8_t reserved1;
	uint16_t txReady;
	uint32_t mode;
	uint32_t baudRate;
	uint16_t inProtoMask;
	uint16_t outProtoMask;
	uint16_t flags;
	uint16_t reserved2;
};

struct [[gnu::packed]] UbxCfgRatePacket {
	uint16_t measRate;
	uint16_t navRate;
	uint16_t timeRef;
};

struct [[gnu::packed]] UbxCfgTP5Packet {
	uint8_t tpIdx;
	uint8_t version;
	uint16_t reserved1;
	uint16_t antCableDelay; /* Signed */
	uint16_t rfGroupDelay; /* Signed */
	uint32_t freqPeriod;
	uint32_t freqPeriodLock;
	uint32_t pulseLenRatio;
	uint32_t pulseLenRatioLock;
	uint32_t userConfigDelay; /* Signed */
	uint32_t flags;
};

struct [[gnu::packed]] UbxNavPosLLHPacket {
	uint32_t iTOW;
	uint32_t lon; /* Signed */
	uint32_t lat; /* Signed */
	uint32_t height; /* Signed */
	uint32_t hMSL; /* Signed */
	uint32_t hAcc;
	uint32_t vAcc;
};

struct [[gnu::packed]] UbxNavSatData
{
  uint8_t gnssId;
  uint8_t svId;
  uint8_t cno;
  uint8_t elev; /* Signed */
  uint16_t azim; /* Signed */
  uint16_t prRes; /* Signed */
  uint32_t flags;
};

struct [[gnu::packed]] UbxNavSatPacket
{
  uint32_t iTOW;
  uint8_t version;
  uint8_t numSvs;
  uint8_t reserved1[2];
  struct UbxNavSatData data[
      (UBLOX_MESSAGE_LENGTH - 8) / sizeof(struct UbxNavSatData)
  ];
};

struct [[gnu::packed]] UbxNavStatusPacket
{
  uint32_t iTOW;
  uint8_t gpsFix;
  uint8_t flags;
  uint8_t fixStat;
  uint8_t flags2;
  uint32_t ttff;
  uint32_t msss;
};

struct [[gnu::packed]] UbxNavVelNEDPacket {
	uint32_t iTOW;
	uint32_t velN; /* Signed */
	uint32_t velE; /* Signed */
	uint32_t velD; /* Signed */
	uint32_t speed;
	uint32_t gSpeed;
	uint32_t heading; /* Signed */
	uint32_t sAcc;
	uint32_t cAcc;
};

struct [[gnu::packed]] UbxTimTpPacket
{
  uint32_t towMS;
  uint32_t towSubMS;
  uint32_t qErr; /* Signed */
  uint16_t week;
  uint8_t flags;
  uint8_t refInfo;
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_GNSS_UBLOX_DEFS_H_ */
