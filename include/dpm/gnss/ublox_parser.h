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
struct UbloxMessage
{
  uint16_t length;
  uint16_t type;

  union
  {
    uint8_t raw[UBLOX_MESSAGE_LENGTH];

    struct UbxNavSatPacket ubxNavSat;
    struct UbxNavStatusPacket ubxNavStatus;
    struct UbxTimTpPacket ubxTimTp;
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
size_t ubloxParserPrepare(uint8_t *, size_t, const struct UbloxMessage *);
size_t ubloxParserProcess(struct UbloxParser *, const uint8_t *, size_t);
bool ubloxParserReady(const struct UbloxParser *);
void ubloxParserReset(struct UbloxParser *);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_GNSS_UBLOX_PARSER_H_ */
