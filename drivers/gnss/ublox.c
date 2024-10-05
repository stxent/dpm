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
  CONFIG_NAV,
  CONFIG_TP,
  CONFIG_RATE_POSLLH,
  CONFIG_RATE_VELNED,
  CONFIG_RATE_TP,
  CONFIG_RATE_SAT,
  CONFIG_RATE_STATUS,
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
  uint64_t timestamp;
  uint64_t timedelta;
  bool queued;

  void *callbackArgument;
  void (*onConfigFinished)(void *, bool);
  void (*onDataReceived)(void *, const uint8_t *, size_t);
  void (*onPositionReceived)(void *, int32_t, int32_t, int32_t);
  void (*onSatelliteCountReceived)(void *, const struct SatelliteInfo *);
  void (*onStatusReceived)(void *, enum FixType);
  void (*onTimeReceived)(void *, uint64_t);
  void (*onVelocityReceived)(void *, int32_t, int32_t, int32_t);
};
/*----------------------------------------------------------------------------*/
static void onMessageReceivedAckAck(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedAckNak(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavSat(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedNavStatus(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedPosLLH(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedTimTp(struct Ublox *,
    const struct UbloxMessage *);
static void onMessageReceivedVelNED(struct Ublox *,
    const struct UbloxMessage *);

static inline uint32_t calcConfigTimeout(const struct Timer *);
static void configMessageRate(struct Ublox *, uint16_t, uint8_t);
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
    {onMessageReceivedPosLLH, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_POSLLH)},
    {onMessageReceivedNavStatus, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_STATUS)},
    {onMessageReceivedNavSat, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_SAT)},
    {onMessageReceivedVelNED, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_VELNED)},
    {onMessageReceivedTimTp, UBLOX_TYPE_PACK(UBX_TIM, UBX_TIM_TP)}
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

    receiver->config.pending = 0;
    receiver->config.state = CONFIG_ERROR;

    wqAdd(receiver->wq, updateConfigState, receiver);
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

  static const uint8_t navStatusFlagsGPSFixOk = 0x01;
  static const uint8_t navStatusFlagsDiffSoln = 0x02;

  if (receiver->onStatusReceived == NULL)
    return;

  const struct UbxNavStatusPacket * const packet = &message->data.ubxNavStatus;
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
      if (packet->flags & navStatusFlagsGPSFixOk)
      {
        if (packet->flags & navStatusFlagsDiffSoln)
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

  receiver->onStatusReceived(receiver->callbackArgument, fix);
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedPosLLH(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  if (receiver->onPositionReceived == NULL)
    return;

  const struct UbxNavPosLLHPacket * const packet = &message->data.ubxPosLLH;
  const int32_t lat = (int32_t)fromLittleEndian32(packet->lat);
  const int32_t lon = (int32_t)fromLittleEndian32(packet->lon);
  const int32_t alt = (int32_t)fromLittleEndian32(packet->height);

  receiver->onPositionReceived(receiver->callbackArgument, lat, lon, alt);
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedTimTp(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  if (receiver->chrono == NULL || !receiver->timestamp)
    return;

  if (timerGetValue64(receiver->chrono) - receiver->timestamp >= 1000000)
    return;

  const struct UbxTimTpPacket * const packet = &message->data.ubxTimTp;
  const uint32_t towMS = fromLittleEndian32(packet->towMS);
  const uint32_t towSubMS = fromLittleEndian32(packet->towSubMS);
  const uint16_t week = fromLittleEndian16(packet->week);

  const uint64_t gpsTime = makeGpsTime(week, towMS)
      + ((uint64_t)towSubMS * 1000 / (1ULL << 32)) - 1000000;

  receiver->timedelta = gpsTime - receiver->timestamp;
  receiver->timestamp = 0;
}
/*----------------------------------------------------------------------------*/
static void onMessageReceivedVelNED(struct Ublox *receiver,
    const struct UbloxMessage *message)
{
  if (receiver->onVelocityReceived == NULL)
    return;

  const struct UbxNavVelNEDPacket * const packet = &message->data.ubxVelNED;
  const int32_t velN = (int32_t)fromLittleEndian32(packet->velN);
  const int32_t velE = (int32_t)fromLittleEndian32(packet->velE);
  const int32_t velD = (int32_t)fromLittleEndian32(packet->velD);

  receiver->onVelocityReceived(receiver->callbackArgument, velN, velE, velD);
}
/*----------------------------------------------------------------------------*/
static inline uint32_t calcConfigTimeout(const struct Timer *timer)
{
  static const uint32_t configRequestFreq = 100;
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
  receiver->timestamp = timerGetValue64(receiver->chrono);

  if (receiver->timedelta && receiver->onTimeReceived != NULL)
  {
    receiver->onTimeReceived(receiver->callbackArgument,
        timerGetValue64(receiver->chrono) + receiver->timedelta);
  }
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *argument)
{
  struct Ublox * const receiver = argument;

  if (receiver->config.retries > 0)
  {
    --receiver->config.retries;

    if (wqAdd(receiver->wq, updateConfigState, receiver) != E_OK)
      receiver->config.state = CONFIG_ERROR;
  }
  else
  {
    receiver->config.pending = 0;
    receiver->config.state = CONFIG_ERROR;

    wqAdd(receiver->wq, updateConfigState, receiver);
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

    if (receiver->onDataReceived != NULL)
      receiver->onDataReceived(receiver->callbackArgument, buffer, length);
  }
}
/*----------------------------------------------------------------------------*/
static void updateConfigState(void *argument)
{
  struct Ublox * const receiver = argument;

  switch (receiver->config.state)
  {
    case CONFIG_PORT:
      sendConfigPortMessage(receiver, receiver->config.port,
          receiver->config.rate);
      break;

    case CONFIG_RATE:
      sendConfigRateMessage(receiver, receiver->config.measurements);
      break;

    case CONFIG_NAV:
      sendConfigNavMessage(receiver, receiver->config.elevation);
      break;

    case CONFIG_TP:
      /* Set PPS period to 1 second */
      sendConfigTpMessage(receiver, 1000000);
      break;

    case CONFIG_RATE_POSLLH:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_POSLLH), 1);
      break;

    case CONFIG_RATE_VELNED:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_VELNED), 1);
      break;

    case CONFIG_RATE_TP:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_TIM, UBX_TIM_TP),
          receiver->config.measurements);
      break;

    case CONFIG_RATE_SAT:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_SAT),
          receiver->config.measurements);
      break;

    case CONFIG_RATE_STATUS:
      configMessageRate(receiver, UBLOX_TYPE_PACK(UBX_NAV, UBX_NAV_STATUS),
          receiver->config.measurements);
      break;

    case CONFIG_READY:
      if (receiver->onConfigFinished != NULL)
        receiver->onConfigFinished(receiver->callbackArgument, true);
      return;

    case CONFIG_ERROR:
      if (receiver->onConfigFinished != NULL)
        receiver->onConfigFinished(receiver->callbackArgument, false);
      return;
  }

  timerSetValue(receiver->timer, 0);
  timerEnable(receiver->timer);
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
  {
    ubloxParserReset(&receiver->parser);
    receiver->timedelta = 0;
  }

  ifSetCallback(receiver->serial, onSerialEvent, receiver);

  if (receiver->timer != NULL)
  {
    timerSetAutostop(receiver->timer, true);
    timerSetCallback(receiver->timer, onTimerEvent, receiver);
    timerSetOverflow(receiver->timer, calcConfigTimeout(receiver->timer));
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

  receiver->chrono = config->chrono;
  receiver->serial = config->serial;
  receiver->pps = config->pps;
  receiver->timer = config->timer;
  receiver->wq = config->wq ? config->wq : WQ_DEFAULT;

  receiver->callbackArgument = NULL;
  receiver->onConfigFinished = NULL;
  receiver->onDataReceived = NULL;
  receiver->onPositionReceived = NULL;
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
    void (*callback)(void *, int32_t, int32_t, int32_t))
{
  receiver->onPositionReceived = callback;
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
