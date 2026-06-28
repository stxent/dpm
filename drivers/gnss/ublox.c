/*
 * ublox_parser.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/gnss/ublox.h>
#include <dpm/gnss/ublox_parser.h>
#include <halm/interrupt.h>
#include <halm/timer.h>
#include <halm/wq.h>
#include <xcore/interface.h>
#include <xcore/memory.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
#define BUFFER_LENGTH 256
#define MAX_RETRIES   10

enum ConfigState
{
  CONFIG_START,
  CONFIG_PORT = CONFIG_START,
  CONFIG_RATE,
  CONFIG_ANT,
  CONFIG_NAV,
  CONFIG_TP,
  CONFIG_RATE_PVT,
  CONFIG_RATE_SAT,
  CONFIG_RATE_POSLLH,
  CONFIG_RATE_VELNED,
  CONFIG_RATE_SOL,
  CONFIG_RATE_TIMEGPS,
  CONFIG_READY,
  CONFIG_ERROR
};

struct HandlerEntry
{
  void (*callback)(struct Ublox *, const struct UbloxMessage *);
  uint16_t type;
};

struct Ublox
{
  struct Entity base;

  struct Timer64 *chrono;
  struct Interface *serial;
  struct Interrupt *pps;
  struct Timer *timer;
  struct WorkQueue *wq;

  struct
  {
    uint8_t buffer[sizeof(struct UbloxConfigMessage) + UBLOX_MESSAGE_OVERHEAD];

    uint32_t rate;
    uint16_t pending;
    int8_t elevation;
    uint8_t measurements;
    uint8_t port;
    uint8_t retries;
    uint8_t state;
  } config;

  struct UbloxParser parser;
  uint64_t heartbeat; /* Last message time in local time format */
  uint64_t localtime; /* PPS edge time in local time format */
  uint64_t timedelta; /* Difference between GPS time and local time */
  uint64_t timestamp; /* Solution time in GPS time format */
  uint16_t week; /* Week number without rollovers */
  int8_t leaps; /* Leap seconds */
  bool queued;
  bool solution; /* Solution available */

  void *callbackArgument;
  void (*onConfigFinished)(void *, bool);
  void (*onDataReceived)(void *, const uint8_t *, size_t);
  void (*onPositionReceived)(void *, int32_t, int32_t, int32_t, int32_t);
  void (*onPrecisionReceived)(void *, uint32_t, uint32_t, uint32_t);
  void (*onSatelliteCountReceived)(void *, const struct SatelliteInfo *);
  void (*onStatusReceived)(void *, enum FixType, uint16_t);
  void (*onTimeReceived)(void *, uint64_t, int8_t);
  void (*onVelocityReceived)(void *, int32_t, int32_t, int32_t);
};
/*----------------------------------------------------------------------------*/
static void onMessageReceivedAckAck(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedAckNak(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavPosLLH(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavPVT(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavSat(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavSol(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavTimeGPS(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavVelNED(struct Ublox *,
    const struct UbloxMessage *);

static inline uint32_t calcCheckTimeout(const struct Timer *);
static inline uint32_t calcConfigTimeout(const struct Timer *);
static void configMessageRate(struct Ublox *, uint16_t, uint8_t);
static void sendConfigAntMessage(struct Ublox *);
static void sendConfigNavMessage(struct Ublox *, int8_t);
static void sendConfigPortMessage(struct Ublox *, uint8_t, uint32_t);
static void sendConfigRateMessage(struct Ublox *, uint16_t);
static void sendConfigTpMessage(struct Ublox *, uint32_t);

static uint64_t makeGpsTime(uint16_t, uint32_t);
static void onMessageReceived(struct Ublox *, const struct UbloxMessage *);
static void onSerialEvent(void *);
static void onTimePulseEvent(void *);
static void onTimerEvent(void *);
static void parseSerialDataTask(void *);
static void updateConfigState(void *);

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
    {onMessageReceivedAckAck, UBLOX_TYPE_PACK(UBX_ACK, UBX_ACK_ACK)},
    {onMessageReceivedAckNak, UBLOX_TYPE_PACK(UBX_ACK, UBX_ACK_NAK)},
    {onMessageReceivedNavPosLLH, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_POSLLH)},
    {onMessageReceivedNavPVT, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_PVT)},
    {onMessageReceivedNavSat, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_SAT)},
    {onMessageReceivedNavSol, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_SOL)},
    {onMessageReceivedNavTimeGPS, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_TIMEGPS)},
    {onMessageReceivedNavVelNED, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_VELNED)}
};
/*----------------------------------------------------------------------------*/
static void onMessageReceivedAckAck(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  const struct UbxAckAckPacket * const packet = &message->data.ubxAckAck;
  const uint16_t type = UBLOX_TYPE_PACK(packet->clsID, packet->msgID);

  if (type == receiver->config.pending)
  {
    timerDisable(receiver->timer);

    if (receiver->config.state == CONFIG_RATE_SAT)
    {
      /* Skip old position messages */
      receiver->config.state = CONFIG_RATE_TIMEGPS;
    }
    else
      ++receiver->config.state;

    receiver->config.pending = 0;
    receiver->config.retries = MAX_RETRIES;

    if (wqAdd(receiver->wq, updateConfigState, receiver) != E_OK)
      receiver->config.state = CONFIG_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedAckNak(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  const struct UbxAckNakPacket * const packet = &message->data.ubxAckNak;
  const uint16_t type = UBLOX_TYPE_PACK(packet->clsID, packet->msgID);

  if (type == receiver->config.pending)
  {
    timerDisable(receiver->timer);

    if (receiver->config.state == CONFIG_RATE_PVT)
    {
      /* Old receiver version, enable compatibility mode */
      receiver->config.state = CONFIG_RATE_POSLLH;
      receiver->config.retries = MAX_RETRIES;
    }
    else
      receiver->config.state = CONFIG_ERROR;

    receiver->config.pending = 0;

    if (wqAdd(receiver->wq, updateConfigState, receiver) != E_OK)
      receiver->config.state = CONFIG_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavPosLLH(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  const struct UbxNavPosLLHPacket * const packet = &message->data.ubxNavPosLLH;

  if (!receiver->solution)
    return;
  receiver->timestamp = makeGpsTime(receiver->week,
      fromLittleEndian32(packet->iTOW));

  if (receiver->onPrecisionReceived != NULL)
  {
    const uint32_t hAcc = packet->hAcc;
    const uint32_t vAcc = packet->vAcc;

    receiver->onPrecisionReceived(receiver->callbackArgument,
        hAcc, vAcc, UINT32_MAX);
  }

  if (receiver->onPositionReceived != NULL)
  {
    const int32_t lat = (int32_t)fromLittleEndian32(packet->lat);
    const int32_t lon = (int32_t)fromLittleEndian32(packet->lon);
    const int32_t alt = (int32_t)fromLittleEndian32(packet->height);
    const int32_t msl = (int32_t)fromLittleEndian32(packet->hMSL);

    receiver->onPositionReceived(receiver->callbackArgument,
        lat, lon, alt, msl);
  }
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavPVT(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  enum GnssFix
  {
    GPSFIX_NOFIX,
    GPSFIX_DEAD_RECKONING,
    GPSFIX_2DFIX,
    GPSFIX_3DFIX,
    GPSFIX_COMBINED,
    GPSFIX_TIME_ONLY
  };

  static const uint8_t navPVTFlagsGnssFixOK = 0x01;
  static const uint8_t navPVTFlagsDiffSoln = 0x02;

  const struct UbxNavPVTPacket * const packet = &message->data.ubxNavPVT;
  enum FixType fix = FIX_NONE;

  if (receiver->onStatusReceived != NULL)
  {
    uint32_t pdop = fromLittleEndian16(packet->pDOP) * 10;

    if (pdop > UINT16_MAX)
      pdop = UINT16_MAX;

    switch (packet->fixType)
    {
      case GPSFIX_DEAD_RECKONING:
        fix = FIX_DEAD_RECKONING;
        break;

      case GPSFIX_2DFIX:
        fix = FIX_2D;
        break;

      case GPSFIX_3DFIX:
        if (packet->flags & navPVTFlagsGnssFixOK)
        {
          if (packet->flags & navPVTFlagsDiffSoln)
            fix = FIX_3D_CORRECTED;
          else
            fix = FIX_3D;
        }
        else
          fix = FIX_2D;
        break;

      default:
        break;
    }

    receiver->onStatusReceived(receiver->callbackArgument, fix, (uint16_t)pdop);
  }

  if (fix >= FIX_3D)
  {
    receiver->timestamp = makeGpsTime(receiver->week,
        fromLittleEndian32(packet->iTOW));
    receiver->solution = true;

    if (receiver->onPrecisionReceived != NULL)
    {
      const uint32_t hAcc = (int32_t)fromLittleEndian32(packet->hAcc);
      const uint32_t vAcc = (int32_t)fromLittleEndian32(packet->vAcc);
      const uint32_t sAcc = (int32_t)fromLittleEndian32(packet->sAcc);

      receiver->onPrecisionReceived(receiver->callbackArgument,
          hAcc, vAcc, sAcc);
    }

    if (receiver->onVelocityReceived != NULL)
    {
      const int32_t velN = (int32_t)fromLittleEndian32(packet->velN);
      const int32_t velE = (int32_t)fromLittleEndian32(packet->velE);
      const int32_t velD = (int32_t)fromLittleEndian32(packet->velD);

      receiver->onVelocityReceived(receiver->callbackArgument,
          velN, velE, velD);
    }

    if (receiver->onPositionReceived != NULL)
    {
      const int32_t lat = (int32_t)fromLittleEndian32(packet->lat);
      const int32_t lon = (int32_t)fromLittleEndian32(packet->lon);
      const int32_t alt = (int32_t)fromLittleEndian32(packet->height);
      const int32_t msl = (int32_t)fromLittleEndian32(packet->hMSL);

      receiver->onPositionReceived(receiver->callbackArgument,
          lat, lon, alt, msl);
    }
  }
  else
  {
    receiver->solution = false;
    receiver->timestamp = 0;
  }
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavSat(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  static const uint32_t navSatQualityMask = 0x00000007UL;

  if (receiver->onSatelliteCountReceived == NULL)
    return;

  const struct UbxNavSatPacket * const packet = &message->data.ubxNavSat;
  const size_t count =
      (message->length - offsetof(struct UbxNavSatPacket, data))
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
        fromLittleEndian32(packet->data[i].flags) & navSatQualityMask;

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

  receiver->onSatelliteCountReceived(receiver->callbackArgument, &satellites);
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavSol(struct Ublox *receiver,
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

  static const uint8_t navSolFlagsGPSFixOk = 0x01;
  static const uint8_t navSolFlagsDiffSoln = 0x02;

  const struct UbxNavSolPacket * const packet = &message->data.ubxNavSol;
  enum FixType fix = FIX_NONE;

  switch (packet->gpsFix)
  {
    case GPSFIX_DEAD_RECKONING:
      fix = FIX_DEAD_RECKONING;
      break;

    case GPSFIX_2DFIX:
      fix = FIX_2D;
      break;

    case GPSFIX_3DFIX:
      if (packet->flags & navSolFlagsGPSFixOk)
      {
        if (packet->flags & navSolFlagsDiffSoln)
          fix = FIX_3D_CORRECTED;
        else
          fix = FIX_3D;
      }
      else
        fix = FIX_2D;
      break;

    default:
      break;
  }

  if (fix < FIX_3D)
  {
    receiver->solution = false;
    receiver->timestamp = 0;
  }
  else
    receiver->solution = true;

  if (receiver->onStatusReceived != NULL)
  {
    uint32_t pdop = fromLittleEndian16(packet->pDOP) * 10;

    if (pdop > UINT16_MAX)
      pdop = UINT16_MAX;

    receiver->onStatusReceived(receiver->callbackArgument, fix, (uint16_t)pdop);
  }

  if (receiver->onSatelliteCountReceived != NULL)
  {
    const struct SatelliteInfo satellites = {packet->numSV, 0, 0, 0, 0};
    receiver->onSatelliteCountReceived(receiver->callbackArgument, &satellites);
  }
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavTimeGPS(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  /* Valid Time of Week and valid Week Number flags */
  static const uint8_t navTimeGPSValidWeekTOW = 0x03;
  /* Valid Leap Seconds flag */
  static const uint8_t navTimeGPSValidUTC = 0x04;

  if (receiver->chrono == NULL || !receiver->localtime)
    return;
  if (timerGetValue64(receiver->chrono) - receiver->localtime >= 1000000)
    return;

  const struct UbxNavTimeGPSPacket * const packet =
      &message->data.ubxNavTimeGPS;

  if ((packet->valid & navTimeGPSValidWeekTOW) == navTimeGPSValidWeekTOW)
  {
    const uint32_t tow = fromLittleEndian32(packet->iTOW);
    const uint16_t week = fromLittleEndian16(packet->week);
    const uint64_t timestamp = makeGpsTime(week, tow - (tow % 1000));

    receiver->timedelta = timestamp - receiver->localtime;
    receiver->week = week;
    receiver->localtime = 0;
  }

  if (packet->valid & navTimeGPSValidUTC)
    receiver->leaps = packet->leapS;
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedNavVelNED(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  const struct UbxNavVelNEDPacket * const packet = &message->data.ubxNavVelNED;

  if (!receiver->solution)
    return;
  receiver->timestamp = makeGpsTime(receiver->week,
      fromLittleEndian32(packet->iTOW));

  if (receiver->onPrecisionReceived != NULL)
  {
    const uint32_t sAcc = packet->sAcc;

    receiver->onPrecisionReceived(receiver->callbackArgument,
        UINT32_MAX, UINT32_MAX, sAcc);
  }

  if (receiver->onVelocityReceived != NULL)
  {
    const int32_t velN = (int32_t)fromLittleEndian32(packet->velN);
    const int32_t velE = (int32_t)fromLittleEndian32(packet->velE);
    const int32_t velD = (int32_t)fromLittleEndian32(packet->velD);

    receiver->onVelocityReceived(receiver->callbackArgument, velN, velE, velD);
  }
}
/*----------------------------------------------------------------------------*/
static inline uint32_t calcCheckTimeout(const struct Timer *timer)
{
  static const uint32_t receptionCheckPeriod = 1; /* Seconds */
  return timerGetFrequency(timer) * receptionCheckPeriod;
}
/*----------------------------------------------------------------------------*/
static inline uint32_t calcConfigTimeout(const struct Timer *timer)
{
  static const uint32_t configRequestFreq = 100; /* Hz */
  return (timerGetFrequency(timer) + configRequestFreq - 1) / configRequestFreq;
}
/*----------------------------------------------------------------------------*/
static void configMessageRate(struct Ublox *receiver, uint16_t type,
    uint8_t rate)
{
  const struct UbxCfgMsgPacket packet = {
      .msgClass = UBLOX_TYPE_GROUP(type),
      .msgID = UBLOX_TYPE_ID(type),
      .rate = rate
  };

  receiver->config.pending = UBLOX_TYPE_PACK(UBX_CFG, UBX_CFG_MSG);

  const size_t length = ubloxParserPrepare(receiver->config.buffer,
      receiver->config.pending, &packet, sizeof(packet));

  ifWrite(receiver->serial, receiver->config.buffer, length);
}
/*----------------------------------------------------------------------------*/
static void sendConfigAntMessage(struct Ublox *receiver)
{
  static const uint16_t cfgAntFlags = 0x0001;

  const struct UbxCfgAntPacket packet = {
      .flags = toLittleEndian16(cfgAntFlags),
      .pins = toLittleEndian16(0)
  };

  receiver->config.pending = UBLOX_TYPE_PACK(UBX_CFG, UBX_CFG_ANT);

  const size_t length = ubloxParserPrepare(receiver->config.buffer,
      receiver->config.pending, &packet, sizeof(packet));

  ifWrite(receiver->serial, receiver->config.buffer, length);
}
/*----------------------------------------------------------------------------*/
static void sendConfigNavMessage(struct Ublox *receiver, int8_t elevation)
{
  /* Update dynamic model and fix mode */
  static const uint16_t cfgNav5Mask = 0x0005;
  /* Update min elevation */
  static const uint16_t cfgNav5MaskMinElev = 0x0002;
  /* Airborne < 2g */
  static const uint8_t cfgNav5DynModel = 7;
  /* 3D only */
  static const uint8_t cfgNav5FixMode = 2;

  const struct UbxCfgNav5Packet packet = {
      .mask = cfgNav5Mask | (elevation ? cfgNav5MaskMinElev : 0),
      .dynModel = cfgNav5DynModel,
      .fixMode = cfgNav5FixMode,
      .fixedAlt = 0, /* Signed */
      .fixedAltVar = 0,
      .minElev = (uint8_t)elevation,
      .drLimit = 0,
      .pDop = 0,
      .tDop = 0,
      .pAcc = 0,
      .tAcc = 0,
      .staticHoldThresh = 0,
      .dgnssTimeout = 0,
      .cnoThreshNumSVs = 0,
      .cnoThresh = 0,
      .reserved1 = {0},
      .staticHoldMaxDist = 0,
      .utcStandard = 0,
      .reserved2 = {0}
  };

  receiver->config.pending = UBLOX_TYPE_PACK(UBX_CFG, UBX_CFG_NAV5);

  const size_t length = ubloxParserPrepare(receiver->config.buffer,
      receiver->config.pending, &packet, sizeof(packet));

  ifWrite(receiver->serial, receiver->config.buffer, length);
}
/*----------------------------------------------------------------------------*/
static void sendConfigPortMessage(struct Ublox *receiver, uint8_t port,
    uint32_t rate)
{
  static const uint32_t cfgPrtMode = 0x000008D0UL;
  static const uint16_t cfgPrtInProtoMask = 0x0001;
  static const uint16_t cfgPrtOutProtoMask = 0x0001;

  const struct UbxCfgPrtPacket packet = {
      .portID = port,
      .reserved1 = 0,
      .txReady = 0,
      .mode = toLittleEndian32(cfgPrtMode),
      .baudRate = toLittleEndian32(rate),
      .inProtoMask = toLittleEndian16(cfgPrtInProtoMask),
      .outProtoMask = toLittleEndian16(cfgPrtOutProtoMask),
      .flags = 0,
      .reserved2 = {0}
  };

  receiver->config.pending = UBLOX_TYPE_PACK(UBX_CFG, UBX_CFG_PRT);

  const size_t length = ubloxParserPrepare(receiver->config.buffer,
      receiver->config.pending, &packet, sizeof(packet));

  ifWrite(receiver->serial, receiver->config.buffer, length);
}
/*----------------------------------------------------------------------------*/
static void sendConfigRateMessage(struct Ublox *receiver, uint16_t rate)
{
  static const uint16_t cfgRateNavRate = 0x0001;
  static const uint16_t cfgRateTimeRef = 0x0001; /* GPS */

  const struct UbxCfgRatePacket packet = {
      .measRate = toLittleEndian16(1000 / rate),
      .navRate = toLittleEndian16(cfgRateNavRate),
      .timeRef = toLittleEndian16(cfgRateTimeRef)
  };

  receiver->config.pending = UBLOX_TYPE_PACK(UBX_CFG, UBX_CFG_RATE);

  const size_t length = ubloxParserPrepare(receiver->config.buffer,
      receiver->config.pending, &packet, sizeof(packet));

  ifWrite(receiver->serial, receiver->config.buffer, length);
}
/*----------------------------------------------------------------------------*/
static void sendConfigTpMessage(struct Ublox *receiver, uint32_t period)
{
  static const int16_t cfgTP5AntCableDelay = 50;
  static const uint32_t cfgTP5Flags = 0x000000F7UL;
  static const uint32_t cfgTP5PulseLenRatioLock = 1000;

  const struct UbxCfgTP5Packet packet = {
      .tpIdx = 0,
      .version = 0,
      .reserved1 = {0},
      .antCableDelay = toLittleEndian16((uint16_t)cfgTP5AntCableDelay),
      .rfGroupDelay = 0,
      .freqPeriod = toLittleEndian32(period),
      .freqPeriodLock = toLittleEndian32(period),
      .pulseLenRatio = 0,
      .pulseLenRatioLock = toLittleEndian32(cfgTP5PulseLenRatioLock),
      .userConfigDelay = 0,
      .flags = toLittleEndian32(cfgTP5Flags)
  };

  receiver->config.pending = UBLOX_TYPE_PACK(UBX_CFG, UBX_CFG_TP5);

  const size_t length = ubloxParserPrepare(receiver->config.buffer,
      receiver->config.pending, &packet, sizeof(packet));

  ifWrite(receiver->serial, receiver->config.buffer, length);
}
/*----------------------------------------------------------------------------*/
static uint64_t makeGpsTime(uint16_t week, uint32_t ms)
{
  if (!week)
    return 0;

  return ((uint64_t)week * (1000 * 3600 * 24 * 7) + ms) * 1000;
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
  uint32_t available;

  ifGetParam(receiver->serial, IF_RX_AVAILABLE, &available);
  if (available && !receiver->queued)
  {
    if (wqAdd(receiver->wq, parseSerialDataTask, receiver) == E_OK)
      receiver->queued = true;
  }
}
/*----------------------------------------------------------------------------*/
static void onTimePulseEvent(void *argument)
{
  struct Ublox * const receiver = argument;
  receiver->localtime = timerGetValue64(receiver->chrono);

  if (receiver->timedelta && receiver->onTimeReceived != NULL)
  {
    receiver->onTimeReceived(receiver->callbackArgument,
        receiver->localtime + receiver->timedelta, receiver->leaps);
  }
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *argument)
{
  static const uint64_t maxInactiveTime = 5000000; /* Microseconds */
  struct Ublox * const receiver = argument;

  if (receiver->config.state == CONFIG_READY)
  {
    const uint64_t localtime = timerGetValue64(receiver->chrono);

    if (localtime - receiver->heartbeat < maxInactiveTime)
    {
      timerSetValue(receiver->timer, 0);
      timerEnable(receiver->timer);
    }
    else
    {
      receiver->config.state = CONFIG_ERROR;
      wqAdd(receiver->wq, updateConfigState, receiver);
    }
  }
  else
  {
    if (!receiver->config.retries)
    {
      receiver->config.pending = 0;
      receiver->config.state = CONFIG_ERROR;
    }
    else
      --receiver->config.retries;

    if (wqAdd(receiver->wq, updateConfigState, receiver) != E_OK)
      receiver->config.state = CONFIG_ERROR;
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
        receiver->heartbeat = timerGetValue64(receiver->chrono);
        onMessageReceived(receiver, ubloxParserData(&receiver->parser));
      }
    }

    if (receiver->onDataReceived != NULL)
      receiver->onDataReceived(receiver->callbackArgument, buffer, length);
  }
}
/*----------------------------------------------------------------------------*/
static void updateConfigState(void *argument)
{
  struct Ublox * const receiver = argument;

  if (receiver->config.state == CONFIG_START)
  {
    /* Synchronously reset the internal state */
    receiver->heartbeat = 0;
    receiver->timedelta = 0;
    receiver->timestamp = 0;
    receiver->week = 0;
    receiver->leaps = 0;
    receiver->solution = false;
  }

  switch (receiver->config.state)
  {
    case CONFIG_PORT:
      sendConfigPortMessage(receiver, receiver->config.port,
          receiver->config.rate);
      break;

    case CONFIG_RATE:
      sendConfigRateMessage(receiver, receiver->config.measurements);
      break;

    case CONFIG_ANT:
      sendConfigAntMessage(receiver);
      break;

    case CONFIG_NAV:
      sendConfigNavMessage(receiver, receiver->config.elevation);
      break;

    case CONFIG_TP:
      /* Set PPS period to 1 second */
      sendConfigTpMessage(receiver, 1000000);
      break;

    case CONFIG_RATE_PVT:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_PVT), 1);
      break;

    case CONFIG_RATE_SAT:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_SAT),
          receiver->config.measurements);
      break;

    case CONFIG_RATE_POSLLH:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_POSLLH), 1);
      break;

    case CONFIG_RATE_VELNED:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_VELNED), 1);
      break;

    case CONFIG_RATE_SOL:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_SOL),
          receiver->config.measurements);
      break;

    case CONFIG_RATE_TIMEGPS:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_TIMEGPS),
          receiver->config.measurements);
      break;

    case CONFIG_READY:
      if (receiver->onConfigFinished != NULL)
        receiver->onConfigFinished(receiver->callbackArgument, true);

      timerSetOverflow(receiver->timer, calcCheckTimeout(receiver->timer));
      break;

    case CONFIG_ERROR:
      if (receiver->onConfigFinished != NULL)
        receiver->onConfigFinished(receiver->callbackArgument, false);
      break;
  }

  if (receiver->config.state != CONFIG_ERROR)
  {
    timerSetValue(receiver->timer, 0);
    timerEnable(receiver->timer);
  }
}
/*----------------------------------------------------------------------------*/
void ubloxDisable(struct Ublox *receiver)
{
  if (receiver->pps != NULL)
  {
    interruptDisable(receiver->pps);
    interruptSetCallback(receiver->pps, NULL, NULL);
  }

  if (receiver->timer != NULL)
  {
    timerDisable(receiver->timer);
    timerSetCallback(receiver->timer, NULL, NULL);
  }

  ifSetCallback(receiver->serial, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
void ubloxEnable(struct Ublox *receiver)
{
  if (receiver->pps != NULL)
    ubloxParserReset(&receiver->parser);

  ifSetCallback(receiver->serial, onSerialEvent, receiver);

  if (receiver->timer != NULL)
  {
    timerSetAutostop(receiver->timer, true);
    timerSetCallback(receiver->timer, onTimerEvent, receiver);
  }

  if (receiver->chrono != NULL && receiver->pps != NULL)
  {
    interruptSetCallback(receiver->pps, onTimePulseEvent, receiver);
    interruptEnable(receiver->pps);
  }
}
/*----------------------------------------------------------------------------*/
void ubloxGetCounters(const struct Ublox *receiver, uint32_t *received,
    uint32_t *errors)
{
  if (received != NULL)
    *received = receiver->parser.received;
  if (errors != NULL)
    *errors = receiver->parser.errors;
}
/*----------------------------------------------------------------------------*/
uint64_t ubloxGetSolutionTime(const struct Ublox *receiver)
{
  return receiver->timestamp;
}
/*----------------------------------------------------------------------------*/
static enum Result ubloxInit(void *object, const void *configBase)
{
  const struct UbloxConfig * const config = configBase;
  struct Ublox * const receiver = object;

  ubloxParserInit(&receiver->parser);
  receiver->heartbeat = 0;
  receiver->localtime = 0;
  receiver->timedelta = 0;
  receiver->timestamp = 0;
  receiver->week = 0;
  receiver->leaps = 0;
  receiver->queued = false;
  receiver->solution = false;

  receiver->chrono = config->chrono;
  receiver->serial = config->serial;
  receiver->pps = config->pps;
  receiver->timer = config->timer;
  receiver->wq = config->wq ? config->wq : WQ_DEFAULT;

  receiver->callbackArgument = NULL;
  receiver->onConfigFinished = NULL;
  receiver->onDataReceived = NULL;
  receiver->onPositionReceived = NULL;
  receiver->onPrecisionReceived = NULL;
  receiver->onSatelliteCountReceived = NULL;
  receiver->onStatusReceived = NULL;
  receiver->onTimeReceived = NULL;
  receiver->onVelocityReceived = NULL;

  receiver->config.rate = 0;
  receiver->config.pending = 0;
  receiver->config.elevation = config->elevation;
  receiver->config.measurements = config->rate ? config->rate : 1;
  receiver->config.port = 1; /* UART 1 */
  receiver->config.retries = 0;
  receiver->config.state = CONFIG_READY;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void ubloxDeinit(void *object)
{
  struct Ublox * const receiver = object;
  ubloxDisable(receiver);
}
/*----------------------------------------------------------------------------*/
void ubloxReset(struct Ublox *receiver, uint32_t rate)
{
  timerDisable(receiver->timer);
  timerSetOverflow(receiver->timer, calcConfigTimeout(receiver->timer));

  receiver->config.pending = 0;
  receiver->config.retries = MAX_RETRIES;
  receiver->config.rate = rate;
  receiver->config.state = CONFIG_START;

  if (wqAdd(receiver->wq, updateConfigState, receiver) != E_OK)
    receiver->config.state = CONFIG_ERROR;
}
/*----------------------------------------------------------------------------*/
void ubloxSetCallbackArgument(struct Ublox *receiver, void *argument)
{
  receiver->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
void ubloxSetConfigFinishedCallback(struct Ublox *receiver,
    void (*callback)(void *, bool))
{
  receiver->onConfigFinished = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetDataReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, const uint8_t *, size_t))
{
  receiver->onDataReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetPositionReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, int32_t, int32_t, int32_t, int32_t))
{
  receiver->onPositionReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetPrecisionReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, uint32_t, uint32_t, uint32_t))
{
  receiver->onPrecisionReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetSatelliteCountReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, const struct SatelliteInfo *))
{
  receiver->onSatelliteCountReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetStatusReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, enum FixType, uint16_t))
{
  receiver->onStatusReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetTimeReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, uint64_t, int8_t))
{
  assert(receiver->chrono != NULL);
  assert(receiver->pps != NULL);
  receiver->onTimeReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetVelocityReceivedCallback(struct Ublox *receiver,
    void (*callback)(void *, int32_t, int32_t, int32_t))
{
  receiver->onVelocityReceived = callback;
}
