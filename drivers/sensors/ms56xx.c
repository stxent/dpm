/*
 * ms56xx.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/ms56xx.h>
#include <dpm/sensors/ms56xx_defs.h>
#include <halm/generic/i2c.h>
#include <halm/generic/spi.h>
#include <halm/timer.h>
#include <xcore/atomic.h>
#include <assert.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
#define LENGTH_COMMAND    1
#define LENGTH_PROM_ENTRY 2
#define LENGTH_SAMPLE     3

enum State
{
  STATE_IDLE,

  STATE_CAL_START,
  STATE_CAL_WRITE,
  STATE_CAL_WRITE_WAIT,
  STATE_CAL_READ,
  STATE_CAL_READ_WAIT,
  STATE_CAL_END,

  STATE_P_START,
  STATE_P_START_WAIT,
  STATE_P_WAIT,
  STATE_P_REQUEST,
  STATE_P_REQUEST_WAIT,
  STATE_P_READ,
  STATE_P_READ_WAIT,

  STATE_T_START,
  STATE_T_START_WAIT,
  STATE_T_WAIT,
  STATE_T_REQUEST,
  STATE_T_REQUEST_WAIT,
  STATE_T_READ,
  STATE_T_READ_WAIT,

  STATE_PROCESS,

  STATE_ERROR_WAIT,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
};
/*----------------------------------------------------------------------------*/
static void calcOffSens5607(const uint16_t *, int32_t, int64_t *, int64_t *);
static void calcOffSens5611(const uint16_t *, int32_t, int64_t *, int64_t *);
static bool checkCrc4(const uint16_t *);
static void getCompensatedResults(const struct MS56XX *, int32_t *, int32_t *);
static void makeTemperatureCompensation5607(int32_t, int32_t, int64_t *,
    int32_t *, int64_t *);
static void makeTemperatureCompensation5611(int32_t, int32_t, int64_t *,
    int32_t *, int64_t *);

static void busInit(struct MS56XX *, bool);
static void calcPressure(struct MS56XX *);
static void calReadNext(struct MS56XX *);
static void calRequestNext(struct MS56XX *);
static void calReset(struct MS56XX *);
static uint16_t fetchParameter(const struct MS56XX *);
static uint32_t fetchSample(const struct MS56XX *);
static void onBusEvent(void *);
static void onTimerEvent(void *);
static inline uint8_t oversamplingToMode(const struct MS56XX *);
static uint32_t oversamplingToTime(const struct MS56XX *);
static void startPressureConversion(struct MS56XX *);
static void startSampleRead(struct MS56XX *);
static void startSampleRequest(struct MS56XX *);
static void startTemperatureConversion(struct MS56XX *);

static enum Result msInit(void *, const void *);
static void msDeinit(void *);
static const char *msGetFormat(const void *);
static enum SensorStatus msGetStatus(const void *);
static void msSetCallbackArgument(void *, void *);
static void msSetErrorCallback(void *, void (*)(void *, enum SensorResult));
static void msSetResultCallback(void *,
    void (*)(void *, const void *, size_t));
static void msSetUpdateCallback(void *, void (*)(void *));
static void msReset(void *);
static void msSample(void *);
static void msStart(void *);
static void msStop(void *);
static bool msUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct SensorClass * const MS56XX = &(const struct SensorClass){
    .size = sizeof(struct MS56XX),
    .init = msInit,
    .deinit = msDeinit,

    .getFormat = msGetFormat,
    .getStatus = msGetStatus,
    .setCallbackArgument = msSetCallbackArgument,
    .setErrorCallback = msSetErrorCallback,
    .setResultCallback = msSetResultCallback,
    .setUpdateCallback = msSetUpdateCallback,
    .reset = msReset,
    .sample = msSample,
    .start = msStart,
    .stop = msStop,
    .update = msUpdate
};
/*----------------------------------------------------------------------------*/
static void calcOffSens5607(const uint16_t *prom, int32_t dt, int64_t *off,
    int64_t *sens)
{
  *off = ((int64_t)prom[PROM_OFF] << 17)
      + (((int64_t)prom[PROM_TCO] * dt) >> 6);

  *sens = ((int64_t)prom[PROM_SENS] << 16)
      + (((int64_t)prom[PROM_TCS] * dt) >> 7);
}
/*----------------------------------------------------------------------------*/
static void calcOffSens5611(const uint16_t *prom, int32_t dt, int64_t *off,
    int64_t *sens)
{
  *off = ((int64_t)prom[PROM_OFF] << 16)
      + (((int64_t)prom[PROM_TCO] * dt) >> 7);

  *sens = ((int64_t)prom[PROM_SENS] << 15)
      + (((int64_t)prom[PROM_TCS] * dt) >> 8);
}
/*----------------------------------------------------------------------------*/
static bool checkCrc4(const uint16_t *prom)
{
  const uint16_t expected = prom[7] & 0x000F;
  uint16_t remainder = 0;
  uint16_t buffer[8];

  memcpy(buffer, prom, sizeof(buffer));

  /* Remove CRC byte */
  buffer[7] &= 0xFF00;

  for (size_t i = 0; i < 16; ++i)
  {
    const uint8_t value = (i & 1) ?
        (uint8_t)buffer[i >> 1] : (uint8_t)(buffer[i >> 1] >> 8);

    remainder ^= value;

    for (size_t bit = 8; bit > 0; --bit)
    {
      if (remainder & 0x8000)
        remainder = (remainder << 1) ^ 0x3000;
      else
        remainder = remainder << 1;
    }
  }

  /* Final 4 bit remainder is CRC value */
  remainder = (remainder >> 12) & 0x000F;

  return remainder == expected;
}
/*----------------------------------------------------------------------------*/
static void getCompensatedResults(const struct MS56XX *sensor,
    int32_t *pressure, int32_t *temperature)
{
  if (sensor->temperature != 0 && sensor->pressure != 0)
  {
    /* Difference between actual and reference temperature */
    const int32_t dt =
        (int32_t)sensor->temperature - ((int32_t)sensor->prom[PROM_TREF] << 8);

    /* Actual temperature */
    int32_t TEMP = 2000 + (((int64_t)dt * sensor->prom[PROM_TEMPSENS]) >> 23);

    /* Calculate offset and sensitivity at actual temperature */
    int64_t OFF;
    int64_t SENS;
    sensor->calculate(sensor->prom, dt, &OFF, &SENS);

    /* Make temperature compensation */
    int64_t OFF2;
    int32_t TEMP2;
    int64_t SENS2;
    sensor->compensate(TEMP, dt, &OFF2, &TEMP2, &SENS2);

    OFF = OFF - OFF2;
    SENS = SENS - SENS2;

    /* Compensated pressure, Pa * 2^8 */
    const int32_t P =
        ((((int64_t)sensor->pressure * SENS) >> 21) - OFF) >> (15 - 8);

    /* Compensated temperature, C * 2^8 */
    const int32_t T =
        ((TEMP - TEMP2) * 83886) >> (23 - 8);

    *pressure = P;
    *temperature = T;
  }
}
/*----------------------------------------------------------------------------*/
static void makeTemperatureCompensation5607(int32_t temperature, int32_t dT,
    int64_t *OFF2, int32_t *T2, int64_t *SENS2)
{
  if (temperature >= 2000)
  {
    *T2 = 0;
    *OFF2 = 0;
    *SENS2 = 0;
  }
  else
  {
    *T2 = (int32_t)(((int64_t)dT * (int64_t)dT) >> 31);
    *OFF2 = 61 * (temperature - 2000) * (temperature - 2000) / 16;
    *SENS2 = 2 * (temperature - 2000) * (temperature - 2000);

    if (temperature < -1500)
    {
      *OFF2 = *OFF2 + 15 * (temperature + 1500) * (temperature + 1500);
      *SENS2 = *SENS2 + 8 * (temperature + 1500) * (temperature + 1500);
    }
  }
}
/*----------------------------------------------------------------------------*/
static void makeTemperatureCompensation5611(int32_t temperature, int32_t dT,
    int64_t *OFF2, int32_t *T2, int64_t *SENS2)
{
  if (temperature >= 2000)
  {
    *T2 = 0;
    *OFF2 = 0;
    *SENS2 = 0;
  }
  else
  {
    *T2 = (int32_t)(((int64_t)dT * (int64_t)dT) >> 31);
    *OFF2 = 5 * (temperature - 2000) * (temperature - 2000) / 2;
    *SENS2 = 5 * (temperature - 2000) * (temperature - 2000) / 4;

    if (temperature < -1500)
    {
      *OFF2 = *OFF2 + 7 * (temperature + 1500) * (temperature + 1500);
      *SENS2 = *SENS2 + 11 * (temperature + 1500) * (temperature + 1500) / 2;
    }
  }
}
/*----------------------------------------------------------------------------*/
static void busInit(struct MS56XX *sensor, bool read)
{
  /* Lock the interface */
  ifSetParam(sensor->bus, IF_ACQUIRE, 0);

  ifSetParam(sensor->bus, IF_ZEROCOPY, 0);
  ifSetCallback(sensor->bus, onBusEvent, sensor);

  if (sensor->rate)
    ifSetParam(sensor->bus, IF_RATE, &sensor->rate);

  if (sensor->address)
  {
    /* I2C bus */
    ifSetParam(sensor->bus, IF_ADDRESS, &sensor->address);

    if (read)
      ifSetParam(sensor->bus, IF_I2C_REPEATED_START, 0);

    /* Start bus watchdog */
    timerSetOverflow(sensor->timer, timerGetFrequency(sensor->timer) / 10);
    timerSetValue(sensor->timer, 0);
    timerEnable(sensor->timer);
  }
  else
  {
    /* SPI bus */
    ifSetParam(sensor->bus, IF_SPI_UNIDIRECTIONAL, 0);
    pinReset(sensor->gpio);
  }
}
/*----------------------------------------------------------------------------*/
static void calcPressure(struct MS56XX *sensor)
{
  int32_t pressure;
  int32_t temperature;

  getCompensatedResults(sensor, &pressure, &temperature);

  sensor->onResultCallback(sensor->callbackArgument,
      &pressure, sizeof(pressure));

  if (atomicLoad(&sensor->flags) & (FLAG_THERMO_LOOP | FLAG_THERMO_SAMPLE))
  {
    sensor->thermometer->onResultCallback(sensor->thermometer->callbackArgument,
        &temperature, sizeof(temperature));
  }
}
/*----------------------------------------------------------------------------*/
static void calReadNext(struct MS56XX *sensor)
{
  ifRead(sensor->bus, sensor->buffer, LENGTH_PROM_ENTRY);
}
/*----------------------------------------------------------------------------*/
static void calRequestNext(struct MS56XX *sensor)
{
  sensor->buffer[0] = CMD_READ_PROM(sensor->parameter);

  busInit(sensor, true);
  ifWrite(sensor->bus, sensor->buffer, LENGTH_COMMAND);
}
/*----------------------------------------------------------------------------*/
static void calReset(struct MS56XX *sensor)
{
  memset(sensor->prom, 0, sizeof(sensor->prom));
  sensor->parameter = 0;

  atomicFetchAnd(&sensor->flags, ~FLAG_READY);
}
/*----------------------------------------------------------------------------*/
static uint16_t fetchParameter(const struct MS56XX *sensor)
{
  const uint8_t * const buffer = sensor->buffer;
  const uint16_t value = (buffer[0] << 8) | buffer[1];

  return value;
}
/*----------------------------------------------------------------------------*/
static uint32_t fetchSample(const struct MS56XX *sensor)
{
  const uint8_t * const buffer = sensor->buffer;
  const uint32_t value = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];

  return value;
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct MS56XX * const sensor = object;
  bool release = false;
  bool waitForTimeout = false;

  timerDisable(sensor->timer);

  if (sensor->address && ifGetParam(sensor->bus, IF_STATUS, 0) != E_OK)
  {
    sensor->state = STATE_ERROR_WAIT;
    timerSetOverflow(sensor->timer, oversamplingToTime(sensor));

    waitForTimeout = true;
    release = true;
  }

  switch (sensor->state)
  {
    case STATE_CAL_WRITE_WAIT:
      sensor->state = STATE_CAL_READ;
      break;

    case STATE_CAL_READ_WAIT:
      sensor->prom[sensor->parameter] = fetchParameter(sensor);
      sensor->state = STATE_CAL_END;

      release = true;
      break;

    case STATE_P_START_WAIT:
      sensor->state = STATE_P_WAIT;
      timerSetOverflow(sensor->timer, oversamplingToTime(sensor));

      waitForTimeout = true;
      release = true;
      break;

    case STATE_P_REQUEST_WAIT:
      sensor->state = STATE_P_READ;
      break;

    case STATE_P_READ_WAIT:
      sensor->pressure = fetchSample(sensor);
      sensor->state = STATE_T_START;

      release = true;
      break;

    case STATE_T_START_WAIT:
      sensor->state = STATE_T_WAIT;
      timerSetOverflow(sensor->timer, oversamplingToTime(sensor));

      waitForTimeout = true;
      release = true;
      break;

    case STATE_T_REQUEST_WAIT:
      sensor->state = STATE_T_READ;
      break;

    case STATE_T_READ_WAIT:
      sensor->temperature = fetchSample(sensor);
      sensor->state = STATE_PROCESS;

      release = true;
      break;

    default:
      break;
  }

  if (waitForTimeout)
  {
    /* Switching between different overflow values requires counter reset */
    timerSetValue(sensor->timer, 0);
    timerEnable(sensor->timer);
  }

  if (release)
  {
    if (!sensor->address)
      pinSet(sensor->gpio);

    ifSetParam(sensor->bus, IF_RELEASE, 0);
  }

  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct MS56XX * const sensor = object;

  switch (sensor->state)
  {
    case STATE_P_WAIT:
      sensor->state = STATE_P_REQUEST;
      break;

    case STATE_T_WAIT:
      sensor->state = STATE_T_REQUEST;
      break;

    case STATE_ERROR_WAIT:
      sensor->state = STATE_ERROR_INTERFACE;
      break;

    default:
      if (sensor->address)
        ifSetParam(sensor->bus, IF_RELEASE, 0);
      sensor->state = STATE_ERROR_TIMEOUT;
      break;
  }

  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static inline uint8_t oversamplingToMode(const struct MS56XX *sensor)
{
  return sensor->oversampling - 1;
}
/*----------------------------------------------------------------------------*/
static uint32_t oversamplingToTime(const struct MS56XX *sensor)
{
  const uint32_t frequency = timerGetFrequency(sensor->timer);
  uint64_t overflow;

  switch (sensor->oversampling)
  {
    case MS56XX_OVERSAMPLING_256:
      /* 0.6 ms */
      overflow = frequency * (60 * (1ULL << 32) / 100000);
      break;

    case MS56XX_OVERSAMPLING_512:
      /* 1.17 ms */
      overflow = frequency * (117 * (1ULL << 32) / 100000);
      break;

    case MS56XX_OVERSAMPLING_1024:
      /* 2.28 ms */
      overflow = frequency * (228 * (1ULL << 32) / 100000);
      break;

    case MS56XX_OVERSAMPLING_2048:
      /* 4.54 ms */
      overflow = frequency * (454 * (1ULL << 32) / 100000);
      break;

    default:
      /* 9.04 ms */
      overflow = frequency * (904 * (1ULL << 32) / 100000);
      break;
  }

  return (overflow + ((1ULL << 32) - 1)) >> 32;
}
/*----------------------------------------------------------------------------*/
static void startPressureConversion(struct MS56XX *sensor)
{
  sensor->buffer[0] = CMD_CONVERT_D1(oversamplingToMode(sensor));

  busInit(sensor, false);
  ifWrite(sensor->bus, sensor->buffer, LENGTH_COMMAND);
}
/*----------------------------------------------------------------------------*/
static void startSampleRead(struct MS56XX *sensor)
{
  ifRead(sensor->bus, sensor->buffer, LENGTH_SAMPLE);
}
/*----------------------------------------------------------------------------*/
static void startSampleRequest(struct MS56XX *sensor)
{
  sensor->buffer[0] = CMD_READ_ADC;

  busInit(sensor, true);
  ifWrite(sensor->bus, sensor->buffer, LENGTH_COMMAND);
}
/*----------------------------------------------------------------------------*/
static void startTemperatureConversion(struct MS56XX *sensor)
{
  sensor->buffer[0] = CMD_CONVERT_D2(oversamplingToMode(sensor));

  busInit(sensor, false);
  ifWrite(sensor->bus, sensor->buffer, LENGTH_COMMAND);
}
/*----------------------------------------------------------------------------*/
static enum Result msInit(void *object, const void *configBase)
{
  const struct MS56XXConfig * const config = configBase;
  assert(config);
  assert(config->bus);
  assert(config->timer);

  struct MS56XX * const sensor = object;

  sensor->callbackArgument = 0;
  sensor->onErrorCallback = 0;
  sensor->onResultCallback = 0;
  sensor->onUpdateCallback = 0;

  sensor->thermometer = 0;
  sensor->bus = config->bus;
  sensor->timer = config->timer;
  sensor->rate = config->rate;

  sensor->pressure = 0;
  sensor->temperature = 0;
  sensor->flags = 0;
  sensor->state = STATE_IDLE;

  calReset(sensor);

  if (config->oversampling != MS56XX_OVERSAMPLING_DEFAULT)
    sensor->oversampling = (uint8_t)config->oversampling;
  else
    sensor->oversampling = MS56XX_OVERSAMPLING_4096;

  if (config->subtype == MS56XX_TYPE_5607)
  {
    sensor->calculate = calcOffSens5607;
    sensor->compensate = makeTemperatureCompensation5607;
  }
  else
  {
    sensor->calculate = calcOffSens5611;
    sensor->compensate = makeTemperatureCompensation5611;
  }

  if (config->cs)
  {
    sensor->address = 0;

    sensor->gpio = pinInit(config->cs);
    if (!pinValid(sensor->gpio))
      return E_VALUE;
    pinOutput(sensor->gpio, true);
  }
  else
  {
    if (!config->address)
      return E_VALUE;

    sensor->address = config->address;
  }

  timerSetAutostop(sensor->timer, true);
  timerSetCallback(sensor->timer, onTimerEvent, sensor);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void msDeinit(void *object)
{
  struct MS56XX * const sensor = object;

  timerDisable(sensor->timer);
  timerSetCallback(sensor->timer, 0, 0);

  if (sensor->thermometer)
    deinit(sensor->thermometer);
}
/*----------------------------------------------------------------------------*/
static const char *msGetFormat(const void *object __attribute__((unused)))
{
  return "i24q8";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus msGetStatus(const void *object)
{
  const struct MS56XX * const sensor = object;

  if (atomicLoad(&sensor->flags) & FLAG_READY)
  {
    if (sensor->state == STATE_IDLE)
      return SENSOR_IDLE;
    else
      return SENSOR_BUSY;
  }
  else
    return SENSOR_ERROR;
}
/*----------------------------------------------------------------------------*/
static void msSetCallbackArgument(void *object, void *argument)
{
  struct MS56XX * const sensor = object;
  sensor->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
static void msSetErrorCallback(void *object,
    void (*callback)(void *, enum SensorResult))
{
  struct MS56XX * const sensor = object;
  sensor->onErrorCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void msSetResultCallback(void *object,
    void (*callback)(void *, const void *, size_t))
{
  struct MS56XX * const sensor = object;
  sensor->onResultCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void msSetUpdateCallback(void *object, void (*callback)(void *))
{
  struct MS56XX * const sensor = object;
  sensor->onUpdateCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void msReset(void *object)
{
  struct MS56XX * const sensor = object;

  atomicFetchOr(&sensor->flags, FLAG_RESET);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void msSample(void *object)
{
  struct MS56XX * const sensor = object;

  assert(sensor->onResultCallback);
  assert(sensor->onUpdateCallback);

  atomicFetchOr(&sensor->flags, FLAG_SAMPLE);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void msStart(void *object)
{
  struct MS56XX * const sensor = object;

  assert(sensor->onResultCallback);
  assert(sensor->onUpdateCallback);

  atomicFetchOr(&sensor->flags, FLAG_LOOP);
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void msStop(void *object)
{
  struct MS56XX * const sensor = object;

  atomicFetchAnd(&sensor->flags, ~(FLAG_RESET | FLAG_LOOP | FLAG_SAMPLE));
  sensor->onUpdateCallback(sensor->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static bool msUpdate(void *object)
{
  struct MS56XX * const sensor = object;
  bool busy;
  bool updated;

  do
  {
    busy = false;
    updated = false;

    switch (sensor->state)
    {
      case STATE_IDLE:
      {
        const uint8_t flags = atomicLoad(&sensor->flags);

        if (flags & FLAG_RESET)
        {
          sensor->state = STATE_CAL_START;
          updated = true;
        }
        else if (flags & (FLAG_LOOP | FLAG_SAMPLE))
        {
          if (flags & FLAG_READY)
          {
            sensor->state = STATE_P_START;
            updated = true;
          }
        }
        break;
      }

      case STATE_CAL_START:
        sensor->state = STATE_CAL_WRITE;
        calReset(sensor);
        updated = true;
        break;

      case STATE_CAL_WRITE:
        sensor->state = STATE_CAL_WRITE_WAIT;
        calRequestNext(sensor);
        busy = true;
        break;

      case STATE_CAL_WRITE_WAIT:
        busy = true;
        break;

      case STATE_CAL_READ:
        sensor->state = STATE_CAL_READ_WAIT;
        calReadNext(sensor);
        busy = true;
        break;

      case STATE_CAL_READ_WAIT:
        busy = true;
        break;

      case STATE_CAL_END:
        if (++sensor->parameter == ARRAY_SIZE(sensor->prom))
        {
          const bool calibrated = checkCrc4(sensor->prom);

          if (!calibrated)
          {
            atomicFetchAnd(&sensor->flags, ~(FLAG_RESET | FLAG_READY));

            if (sensor->onErrorCallback)
            {
              sensor->onErrorCallback(sensor->callbackArgument,
                  SENSOR_CALIBRATION_ERROR);
            }
          }
          else
          {
            atomicFetchAnd(&sensor->flags, ~FLAG_RESET);
            atomicFetchOr(&sensor->flags, FLAG_READY);
          }

          sensor->state = STATE_IDLE;
        }
        else
        {
          sensor->state = STATE_CAL_WRITE;
        }

        updated = true;
        break;

      case STATE_P_START:
        sensor->state = STATE_P_START_WAIT;
        startPressureConversion(sensor);
        busy = true;
        break;

      case STATE_P_START_WAIT:
        busy = true;
        break;

      case STATE_P_WAIT:
        break;

      case STATE_P_REQUEST:
        sensor->state = STATE_P_REQUEST_WAIT;
        startSampleRequest(sensor);
        busy = true;
        break;

      case STATE_P_REQUEST_WAIT:
        busy = true;
        break;

      case STATE_P_READ:
        sensor->state = STATE_P_READ_WAIT;
        startSampleRead(sensor);
        busy = true;
        break;

      case STATE_P_READ_WAIT:
        busy = true;
        break;

      case STATE_T_START:
        sensor->state = STATE_T_START_WAIT;
        startTemperatureConversion(sensor);
        busy = true;
        break;

      case STATE_T_START_WAIT:
        busy = true;
        break;

      case STATE_T_WAIT:
        break;

      case STATE_T_REQUEST:
        sensor->state = STATE_T_REQUEST_WAIT;
        startSampleRequest(sensor);
        busy = true;
        break;

      case STATE_T_REQUEST_WAIT:
        busy = true;
        break;

      case STATE_T_READ:
        sensor->state = STATE_T_READ_WAIT;
        startSampleRead(sensor);
        busy = true;
        break;

      case STATE_T_READ_WAIT:
        busy = true;
        break;

      case STATE_PROCESS:
        calcPressure(sensor);

        sensor->state = STATE_IDLE;
        atomicFetchAnd(&sensor->flags, ~(FLAG_SAMPLE | FLAG_THERMO_SAMPLE));

        updated = true;
        break;

      case STATE_ERROR_WAIT:
        break;

      case STATE_ERROR_INTERFACE:
      case STATE_ERROR_TIMEOUT:
        if (sensor->onErrorCallback)
        {
          sensor->onErrorCallback(sensor->callbackArgument,
              sensor->state == STATE_ERROR_INTERFACE ?
                  SENSOR_INTERFACE_ERROR : SENSOR_INTERFACE_TIMEOUT);
        }

        sensor->state = STATE_IDLE;
        updated = true;
        break;
    }
  }
  while (updated);

  return busy;
}
/*----------------------------------------------------------------------------*/
struct MS56XXThermometer *ms56xxMakeThermometer(struct MS56XX *sensor)
{
  if (!sensor->thermometer)
  {
    const struct MS56XXThermometerConfig config = {
        .parent = sensor
    };

    sensor->thermometer = init(MS56XXThermometer, &config);
  }

  return sensor->thermometer;
}
