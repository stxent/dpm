/*
 * ublox_parser.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/drivers/gnss/ublox.h>
#include <dpm/drivers/gnss/ublox_defs.h>
#include <halm/wq.h>
#include <xcore/memory.h>
/*----------------------------------------------------------------------------*/
#define BUFFER_LENGTH 256
/*----------------------------------------------------------------------------*/
struct HandlerEntry
{
  void (*callback)(struct Ublox *, const struct UbloxMessage *);
  uint16_t type;
};
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavSat(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavStatus(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedTimTp(struct Ublox *,
    const struct UbloxMessage *);

static uint64_t makeGpsTime(uint16_t, uint32_t);
static void onMessageReceived(struct Ublox *, const struct UbloxMessage *);
static void onSerialEvent(void *);
static void onTimePulseEvent(void *);
static void parseSerialDataTask(void *);

static enum Result ubloxInit(void *, const void *);
static void ubloxDeinit(void *);
/*----------------------------------------------------------------------------*/
const struct EntityClass * const Ublox = &(const struct EntityClass){
    .size = sizeof(struct Ublox),
    .init = ubloxInit,
    .deinit = ubloxDeinit
};
/*----------------------------------------------------------------------------*/
static const struct HandlerEntry handlers[] = {
    {onMessageReceivedNavSat, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_SAT)},
    {onMessageReceivedNavStatus, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_STATUS)},
    {onMessageReceivedTimTp, UBLOX_TYPE_PACK(UBX_TIM, UBX_TIM_TP)}
};
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavSat(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  static const uint32_t QUALITY_MASK = 0x00000007UL;

  const struct UbxNavSatPacket * const packet =
      (const struct UbxNavSatPacket *)message->data;
  const size_t count = (message->length - sizeof(struct UbxNavSatPacket))
      / sizeof(struct UbxNavSatData);

  if (count != packet->numSvs)
  {
    /* Incorrect packet */
    return;
  }

  struct SatelliteInfo satellites = {0, 0, 0, 0, 0};

  for (size_t i = 0; i < count; ++i)
  {
    const uint32_t quality =
        fromLittleEndian32(packet->data[i].flags) & QUALITY_MASK;

    /* Ignore when no signal, unusable or not found */
    if (quality < 2 || quality == 3)
      continue;

    switch ((enum UbloxSystemId)packet->data[i].gnssId)
    {
      case UBX_SYSTEM_GPS:
        ++satellites.gps;
        break;

      case UBX_SYSTEM_GALILEO:
        ++satellites.galileo;
        break;

      case UBX_SYSTEM_BEIDOU:
        ++satellites.beidou;
        break;

      case UBX_SYSTEM_GLONASS:
        ++satellites.glonass;
        break;

      case UBX_SYSTEM_SBAS:
      case UBX_SYSTEM_IMES:
      case UBX_SYSTEM_QZSS:
        ++satellites.sbas;
        break;

      default:
        break;
    }
  }

  if (receiver->onSatelliteCountReceived)
    receiver->onSatelliteCountReceived(receiver->callbackArgument, &satellites);
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavStatus(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  enum GPSFix
  {
    GPSFIX_NOFIX,
    GPSFIX_DEAD_RECKONING,
    GPSFIX_2DFIX,
    GPSFIX_3DFIX,
    GPSFIX_COMBINED,
    GPSFIX_TIME_ONLY
  };

  static const uint8_t FLAGS_GPS_FIX_OK = 0x01;
  static const uint8_t FLAGS_DIFF_SOLN  = 0x02;
  // static const uint8_t FLAGS_WKN_SET    = 0x04; // TODO
  // static const uint8_t FLAGS_TOW_SET    = 0x08; // TODO

  const struct UbxNavStatusPacket * const packet =
      (const struct UbxNavStatusPacket *)message->data;
  enum FixType fix;

  switch (packet->gpsFix)
  {
    case GPSFIX_DEAD_RECKONING:
      fix = FIX_DEAD_RECKONING;
      break;

    case GPSFIX_2DFIX:
      fix = FIX_2D;
      break;

    case GPSFIX_3DFIX:
      if (packet->flags & FLAGS_GPS_FIX_OK)
      {
        if (packet->flags & FLAGS_DIFF_SOLN)
          fix = FIX_3D_CORRECTED;
        else
          fix = FIX_3D;
      }
      else
        fix = FIX_2D;
      break;

    default:
      fix = FIX_NONE;
      break;
  }

  if (receiver->onStatusReceived)
    receiver->onStatusReceived(receiver->callbackArgument, fix);
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedTimTp(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  if (!receiver->timestamp)
    return;

  if (timerGetValue64(receiver->timer) - receiver->timestamp >= 1000000)
    return;

  const struct UbxTimTpPacket * const packet =
      (const struct UbxTimTpPacket *)message->data;
  const uint32_t towMS = fromLittleEndian32(packet->towMS);
  const uint32_t towSubMS = fromLittleEndian32(packet->towSubMS);
  const uint16_t week = fromLittleEndian16(packet->week);

  const uint64_t gpsTime = makeGpsTime(week, towMS)
      + ((uint64_t)towSubMS * 1000 / (1ULL << 32)) - 1000000;

  receiver->timedelta = gpsTime - receiver->timestamp;
  receiver->timestamp = 0;
}
/*----------------------------------------------------------------------------*/
static uint64_t makeGpsTime(uint16_t week, uint32_t ms)
{
  return (uint64_t)ms * 1000 + (uint64_t)week * 7 * 24 * 3600 * 1000000;
}
/*----------------------------------------------------------------------------*/
static void onMessageReceived(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  for (size_t i = 0; i < ARRAY_SIZE(handlers); ++i)
  {
    if (handlers[i].type == message->type)
    {
      handlers[i].callback(receiver, message);
      break;
    }
  }
}
/*----------------------------------------------------------------------------*/
static void onSerialEvent(void *argument)
{
  struct Ublox * const receiver = argument;

  if (!receiver->queued)
  {
    if (wqAdd(receiver->wq, parseSerialDataTask, receiver) == E_OK)
      receiver->queued = true;
  }
}
/*----------------------------------------------------------------------------*/
static void onTimePulseEvent(void *argument)
{
  struct Ublox * const receiver = argument;
  receiver->timestamp = timerGetValue64(receiver->timer);

  if (receiver->timedelta && receiver->onTimeReceived)
  {
    receiver->onTimeReceived(receiver->callbackArgument,
        timerGetValue64(receiver->timer) + receiver->timedelta);
  }
}
/*----------------------------------------------------------------------------*/
static void parseSerialDataTask(void *argument)
{
  struct Ublox * const receiver = argument;
  uint8_t buffer[BUFFER_LENGTH];
  size_t length;

  receiver->queued = false;

  while ((length = ifRead(receiver->serial, buffer, sizeof(buffer))))
  {
    const uint8_t *position = buffer;
    size_t remaining = length;

    while (remaining)
    {
      const size_t parsed = ubloxParserProcess(&receiver->parser,
          position, remaining);

      remaining -= parsed;
      position += parsed;

      if (ubloxParserReady(&receiver->parser))
      {
        onMessageReceived(receiver, ubloxParserData(&receiver->parser));
      }
    }

    if (receiver->onDataReceived)
      receiver->onDataReceived(receiver->callbackArgument, buffer, length);
  }
}
/*----------------------------------------------------------------------------*/
void ubloxDisable(struct Ublox *receiver)
{
  interruptDisable(receiver->pps);
  interruptSetCallback(receiver->pps, 0, 0);

  ifSetCallback(receiver->serial, 0, 0);
}
/*----------------------------------------------------------------------------*/
void ubloxEnable(struct Ublox *receiver)
{
  ubloxParserReset(&receiver->parser);
  receiver->timedelta = 0;

  ifSetCallback(receiver->serial, onSerialEvent, receiver);

  interruptSetCallback(receiver->pps, onTimePulseEvent, receiver);
  interruptEnable(receiver->pps);
}
/*----------------------------------------------------------------------------*/
void ubloxGetCounters(const struct Ublox *receiver, uint32_t *received,
    uint32_t *errors)
{
  if (received)
    *received = receiver->parser.received;
  if (errors)
    *errors = receiver->parser.errors;
}
/*----------------------------------------------------------------------------*/
static enum Result ubloxInit(void *object, const void *configBase)
{
  const struct UbloxConfig * const config = configBase;
  struct Ublox * const receiver = object;

  ubloxParserInit(&receiver->parser);
  receiver->timestamp = 0;
  receiver->timedelta = 0;
  receiver->queued = false;

  receiver->serial = config->serial;
  receiver->pps = config->pps;
  receiver->timer = config->timer;
  receiver->wq = config->wq ? config->wq : WQ_DEFAULT;

  receiver->callbackArgument = 0;
  receiver->onDataReceived = 0;
  receiver->onTimeReceived = 0;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void ubloxDeinit(void *object)
{
  ubloxDisable(object);
}
/*----------------------------------------------------------------------------*/
void ubloxSetCallbackArgument(struct Ublox *receiver, void *argument)
{
  receiver->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
void ubloxSetDataReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, const uint8_t *, size_t))
{
  receiver->onDataReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetSatelliteCountReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, const struct SatelliteInfo *))
{
  receiver->onSatelliteCountReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetStatusReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, enum FixType))
{
  receiver->onStatusReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetTimeReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, uint64_t))
{
  receiver->onTimeReceived = callback;
}
