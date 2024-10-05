/*
 * gnss/ublox_parser.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_GNSS_UBLOX_PARSER_H_
#define DPM_GNSS_UBLOX_PARSER_H_
/*----------------------------------------------------------------------------*/
#include <dpm/gnss/ublox_defs.h>
#include <xcore/helpers.h>
#include <stdbool.h>
#include <stddef.h>
/*----------------------------------------------------------------------------*/
struct UbloxConfigMessage
{
  union
  {
    struct UbxCfgMsgPacket ubxCfgMsg;
    struct UbxCfgNav5Packet ubxCfgNav5;
    struct UbxCfgPrtPacket ubxCfgPrt;
    struct UbxCfgRatePacket ubxCfgRate;
    struct UbxCfgTP5Packet ubxCfgTP5;
  };
};

struct UbloxMessage
{
  uint16_t length;
  uint16_t type;

  union
  {
    uint8_t raw[UBLOX_MESSAGE_LENGTH];

    struct UbxAckAckPacket ubxAckAck;
    struct UbxAckNakPacket ubxAckNak;
    struct UbxNavPosLLHPacket ubxPosLLH;
    struct UbxNavSatPacket ubxNavSat;
    struct UbxNavStatusPacket ubxNavStatus;
    struct UbxTimTpPacket ubxTimTp;
    struct UbxNavVelNEDPacket ubxVelNED;
  } data;
};

struct UbloxParser
{
  struct UbloxMessage message;
  size_t position;

  uint32_t errors;
  uint32_t received;

  uint8_t checksum[2];
  uint8_t state;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void ubloxParserInit(struct UbloxParser *);
const struct UbloxMessage *ubloxParserData(const struct UbloxParser *);
size_t ubloxParserPrepare(uint8_t *, uint16_t, const void *, size_t);
size_t ubloxParserProcess(struct UbloxParser *, const uint8_t *, size_t);
bool ubloxParserReady(const struct UbloxParser *);
void ubloxParserReset(struct UbloxParser *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_GNSS_UBLOX_PARSER_H_ */
