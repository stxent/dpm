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
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const Ublox;

struct Ublox;

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
