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
static void onMessageReceivedTimTp(struct Ublox *, const struct UbloxMessage *);

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
    {onMessageReceivedTimTp, UBLOX_TYPE_PACK(UBX_TIM, UBX_TIM_TP)}
};
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
    receiver->queued = true;
    wqAdd(receiver->wq, parseSerialDataTask, receiver);
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
void ubloxSetCallbackArgument(struct Ublox *ublox, void *argument)
{
  ublox->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
void ubloxSetDataReceivedCallback(struct Ublox *ublox,
    void (*callback)(void *, const uint8_t *, size_t))
{
  ublox->onDataReceived = callback;
}
/*----------------------------------------------------------------------------*/
void ubloxSetTimeReceivedCallback(struct Ublox *ublox,
    void (*callback)(void *, uint64_t))
{
  ublox->onTimeReceived = callback;
}
