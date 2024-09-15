/*
 * gnss/ublox.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_GNSS_UBLOX_H_
#define DPM_GNSS_UBLOX_H_
/*----------------------------------------------------------------------------*/
#include <dpm/gnss/gnss.h>
#include <xcore/entity.h>
#include <stdbool.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const Ublox;

struct Ublox;

struct UbloxConfig
{
  /** Optional: 64-bit chrono timer. */
  void *chrono;
  /** Optional: external interrupt for PPS. */
  void *pps;
  /** Mandatory: serial interface. */
  void *serial;
  /** Optional: timer for delay measurements. */
  void *timer;
  /** Optional: work queue for packet processing tasks. */
  void *wq;
  /** Optional: measurement rate in Hz. */
  uint32_t rate;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void ubloxDisable(struct Ublox *);
void ubloxEnable(struct Ublox *);
void ubloxGetCounters(const struct Ublox *, uint32_t *, uint32_t *);
void ubloxReset(struct Ublox *, uint32_t);
void ubloxSetCallbackArgument(struct Ublox *, void *);
void ubloxSetConfigFinishedCallback(struct Ublox *,
    void (*)(void *, bool));
void ubloxSetDataReceivedCallback(struct Ublox *,
    void (*)(void *, const uint8_t *, size_t));
void ubloxSetPositionReceivedCallback(struct Ublox *,
    void (*)(void *, int32_t, int32_t, int32_t));
void ubloxSetSatelliteCountReceivedCallback(struct Ublox *,
    void (*)(void *, const struct SatelliteInfo *));
void ubloxSetStatusReceivedCallback(struct Ublox *,
    void (*)(void *, enum FixType));
void ubloxSetTimeReceivedCallback(struct Ublox *,
    void (*)(void *, uint64_t));
void ubloxSetVelocityReceivedCallback(struct Ublox *,
    void (*)(void *, int32_t, int32_t, int32_t));

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_GNSS_UBLOX_H_ */
