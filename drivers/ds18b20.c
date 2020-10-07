/*
 * ds18b20.c
 * Copyright (C) 2016 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <dpm/drivers/ds18b20.h>
#include <stdbool.h>
/*----------------------------------------------------------------------------*/
enum State
{
  SENSOR_IDLE,
  SENSOR_START_CONVERSION,
  SENSOR_READ_TEMPERATURE,
  SENSOR_READ_SCRATCHPAD,
  SENSOR_ERROR
};
/*----------------------------------------------------------------------------*/
static void sensorCallback(void *);
/*----------------------------------------------------------------------------*/
static enum Result sensorInit(void *, const void *);
static void sensorDeinit(void *);
/*----------------------------------------------------------------------------*/
static const struct EntityClass sensorTable = {
    .size = sizeof(struct DS18B20),
    .init = sensorInit,
    .deinit = sensorDeinit
};
/*----------------------------------------------------------------------------*/
const struct EntityClass * const DS18B20 = &sensorTable;
/*----------------------------------------------------------------------------*/
static const uint8_t readScratchpadCommand[] = {0xBE};
static const uint8_t startConversionCommand[] = {0x44};
/*----------------------------------------------------------------------------*/
static void sensorCallback(void *object)
{
  struct DS18B20 * const sensor = object;
  bool event = false;

  switch (sensor->state)
  {
    case SENSOR_START_CONVERSION:
    {
      sensor->state = SENSOR_IDLE;
      event = true;
      break;
    }

    case SENSOR_READ_TEMPERATURE:
    {
      const uint32_t count = ifRead(sensor->bus, sensor->scratchpad,
          sizeof(sensor->scratchpad));

      sensor->state = count == sizeof(sensor->scratchpad) ?
          SENSOR_READ_SCRATCHPAD : SENSOR_ERROR;
      break;
    }

    case SENSOR_READ_SCRATCHPAD:
    {
      // TODO Check CRC
      sensor->state = SENSOR_IDLE;
      event = true;
      break;
    }

    default:
      break;
  }

  if (event && sensor->callback)
    sensor->callback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
void ds18b20Callback(struct DS18B20 *sensor, void (*callback)(void *),
    void *argument)
{
  sensor->callbackArgument = argument;
  sensor->callback = callback;
}
/*----------------------------------------------------------------------------*/
enum Result ds18b20ReadTemperature(const struct DS18B20 *sensor,
    int16_t *temperature)
{
  if (sensor->state == SENSOR_IDLE)
  {
    const int16_t buffer =
        (sensor->scratchpad[0] | (sensor->scratchpad[1] << 8)) << 4;

    *temperature = buffer;
    return E_OK;
  }
  else
    return E_BUSY;
}
/*----------------------------------------------------------------------------*/
void ds18b20RequestTemperature(struct DS18B20 *sensor)
{
  if (ifSetParam(sensor->bus, IF_ADDRESS, &sensor->address) != E_OK)
  {
    sensor->state = SENSOR_ERROR;
    return;
  }

  ifSetCallback(sensor->bus, sensorCallback, sensor);
  sensor->state = SENSOR_READ_TEMPERATURE;

  const uint32_t count = ifWrite(sensor->bus, readScratchpadCommand,
      sizeof(readScratchpadCommand));

  if (count != sizeof(readScratchpadCommand))
    sensor->state = SENSOR_ERROR;
}
/*----------------------------------------------------------------------------*/
void ds18b20StartConversion(struct DS18B20 *sensor)
{
  if (ifSetParam(sensor->bus, IF_ADDRESS, &sensor->address) != E_OK)
  {
    sensor->state = SENSOR_ERROR;
    return;
  }

  ifSetCallback(sensor->bus, sensorCallback, sensor);
  sensor->state = SENSOR_START_CONVERSION;

  const uint32_t count = ifWrite(sensor->bus, startConversionCommand,
      sizeof(startConversionCommand));

  if (count != sizeof(startConversionCommand))
    sensor->state = SENSOR_ERROR;
}
/*----------------------------------------------------------------------------*/
enum Result ds18b20Status(const struct DS18B20 *sensor)
{
  switch (sensor->state)
  {
    case SENSOR_IDLE:
      return E_OK;

    case SENSOR_ERROR:
      return E_ERROR;

    default:
      return E_BUSY;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result sensorInit(void *object, const void *configBase)
{
  const struct DS18B20Config * const config = configBase;
  struct DS18B20 * const sensor = object;

  sensor->address = config->address;
  sensor->bus = config->bus;
  sensor->callback = 0;
  sensor->state = SENSOR_IDLE;

  return ifSetParam(sensor->bus, IF_ZEROCOPY, 0);
}
/*----------------------------------------------------------------------------*/
static void sensorDeinit(void *object)
{
  struct DS18B20 * const sensor = object;
  ifSetCallback(sensor->bus, 0, 0);
}
