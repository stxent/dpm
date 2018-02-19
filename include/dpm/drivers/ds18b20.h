/*
 * drivers/ds18b20.h
 * Copyright (C) 2016 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#ifndef DPM_DRIVERS_DS18B20_H_
#define DPM_DRIVERS_DS18B20_H_
/*----------------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include <xcore/entity.h>
#include <xcore/interface.h>
/*----------------------------------------------------------------------------*/
extern const struct EntityClass * const DS18B20;
/*----------------------------------------------------------------------------*/
struct DS18B20Config
{
  /** Mandatory: sensor bus. */
  struct Interface *bus;
  /** Optional: device address. */
  uint64_t address;
};
/*----------------------------------------------------------------------------*/
struct DS18B20
{
  struct Entity base;

  void (*callback)(void *);
  void *callbackArgument;

  /* Sensor bus */
  struct Interface *bus;
  /* Device address */
  uint64_t address;

  /* Scratchpad buffer */
  uint8_t scratchpad[9];
  /* Sensor state */
  uint8_t state;
};
/*----------------------------------------------------------------------------*/
void ds18b20Callback(struct DS18B20 *, void (*)(void *), void *);
enum Result ds18b20ReadTemperature(const struct DS18B20 *, int16_t *);
void ds18b20RequestTemperature(struct DS18B20 *);
void ds18b20StartConversion(struct DS18B20 *);
enum Result ds18b20Status(const struct DS18B20 *);
/*----------------------------------------------------------------------------*/
#endif /* DPM_DRIVERS_DS18B20_H_ */
