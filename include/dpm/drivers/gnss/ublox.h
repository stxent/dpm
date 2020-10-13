/*
 * drivers/gnss/ublox.h
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_GNSS_UBLOX_H_
#define DPM_DRIVERS_GNSS_UBLOX_H_
/*----------------------------------------------------------------------------*/
#include <dpm/drivers/gnss/ublox_parser.h>
#include <halm/interrupt.h>
#include <halm/timer.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const Ublox;

struct UbloxConfig
{
  /** Mandatory: serial interface. */
  struct Interface *serial;
  /** Mandatory: external interrupt for PPS. */
  struct Interrupt *pps;
  /** Mandatory: chrono timer. */
  struct Timer64 *timer;
};

struct Ublox
{
  struct Entity base;

  struct Interface *serial;
  struct Interrupt *pps;
  struct Timer64 *timer;

  struct UbloxParser parser;
  uint64_t timestamp;
  uint64_t timedelta;

  void *callbackArgument;
  void (*onDataReceived)(void *, const uint8_t *, size_t);
  void (*onTimeReceived)(void *, uint64_t);
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

void ubloxDisable(struct Ublox *);
void ubloxEnable(struct Ublox *);
void ubloxSetCallbackArgument(struct Ublox *, void *);
void ubloxSetDataReceivedCallback(struct Ublox *,
    void (*)(void *, const uint8_t *, size_t));
void ubloxSetTimeReceivedCallback(struct Ublox *,
    void (*)(void *, uint64_t));

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_GNSS_UBLOX_H_ */
