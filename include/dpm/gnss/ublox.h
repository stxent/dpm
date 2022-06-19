/*
 * gnss/ublox.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_GNSS_UBLOX_H_
#define DPM_GNSS_UBLOX_H_
/*----------------------------------------------------------------------------*/
#include <dpm/gnss/gnss.h>
#include <dpm/gnss/ublox_parser.h>
#include <halm/interrupt.h>
#include <halm/timer.h>
#include <halm/wq.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const Ublox;

struct UbloxConfig
{
  /** Mandatory: external interrupt for PPS. */
  void *pps;
  /** Mandatory: serial interface. */
  void *serial;
  /** Mandatory: 64-bit chrono timer. */
  void *timer;
  /** Optional: work queue for packet processing tasks. */
  void *wq;
};

struct Ublox
{
  struct Entity base;

  struct Interface *serial;
  struct Interrupt *pps;
  struct Timer64 *timer;
  struct WorkQueue *wq;

  struct UbloxParser parser;
  uint64_t timestamp;
  uint64_t timedelta;
  bool queued;

  void *callbackArgument;
  void (*onDataReceived)(void *, const uint8_t *, size_t);
  void (*onSatelliteCountReceived)(void *, const struct SatelliteInfo *);
  void (*onStatusReceived)(void *, enum FixType);
  void (*onTimeReceived)(void *, uint64_t);
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void ubloxDisable(struct Ublox *);
void ubloxEnable(struct Ublox *);
void ubloxGetCounters(const struct Ublox *, uint32_t *, uint32_t *);
void ubloxSetCallbackArgument(struct Ublox *, void *);
void ubloxSetDataReceivedCallback(struct Ublox *,
    void (*)(void *, const uint8_t *, size_t));
void ubloxSetSatelliteCountReceivedCallback(struct Ublox *,
    void (*)(void *, const struct SatelliteInfo *));
void ubloxSetStatusReceivedCallback(struct Ublox *,
    void (*)(void *, enum FixType));
void ubloxSetTimeReceivedCallback(struct Ublox *,
    void (*)(void *, uint64_t));

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_GNSS_UBLOX_H_ */
