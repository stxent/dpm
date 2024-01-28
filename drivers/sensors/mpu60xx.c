/*
 * mpu60xx.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/mpu60xx.h>
#include <dpm/sensors/mpu60xx_defs.h>
#include <halm/generic/i2c.h>
#include <halm/generic/spi.h>
#include <halm/interrupt.h>
#include <halm/timer.h>
#include <xcore/atomic.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
enum ConfigState
{
  CONFIG_BEGIN,
  CONFIG_PWR_MGMT_RESET = CONFIG_BEGIN,
  CONFIG_RESET_WAIT,
  CONFIG_WHO_AM_I_REQUEST,
  CONFIG_WHO_AM_I_READ,
  CONFIG_PWR_MGMT_REQUEST,
  CONFIG_PWR_MGMT_READ,
  CONFIG_PWR_MGMT_SLEEP,
  CONFIG_SIGNAL_PATH_RESET,
  CONFIG_USER_CTRL,
  CONFIG_STARTUP_WAIT,
  CONFIG_PWR_MGMT_WAKEUP,
  CONFIG_GYRO,
  CONFIG_ACCEL,
  CONFIG_BANDWIDTH,
  CONFIG_RATE,
  CONFIG_INT_PIN,
  CONFIG_INT_ENABLE,
  CONFIG_READY_WAIT,
  CONFIG_END
};

enum State
{
  STATE_IDLE,

  STATE_CONFIG_START,
  STATE_CONFIG_UPDATE,
  STATE_CONFIG_TIMER_WAIT,
  STATE_CONFIG_REQUEST_WAIT,
  STATE_CONFIG_BUS_WAIT,
  STATE_CONFIG_END,

  STATE_SUSPEND_START,
  STATE_SUSPEND_BUS_WAIT,
  STATE_SUSPEND_END,

  STATE_EVENT_WAIT,
  STATE_REQUEST,
  STATE_REQUEST_WAIT,
  STATE_READ,
  STATE_READ_WAIT,

  STATE_PROCESS,

  STATE_ERROR_WAIT,
  STATE_ERROR_DEVICE,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
};
/*----------------------------------------------------------------------------*/
static void busInit(struct MPU60XX *, bool);
static inline uint32_t calcResetTimeout(const struct Timer *);
static void calcValues(struct MPU60XX *);
static void fetchAccelSample(const struct MPU60XX *, int16_t *);
static void fetchGyroSample(const struct MPU60XX *, int16_t *);
static int16_t fetchThermoSample(const struct MPU60XX *);
static inline uint8_t makeAccelConfig(const struct MPU60XX *);
static inline int32_t makeAccelMul(const struct MPU60XX *);
static inline uint8_t makeGyroConfig(const struct MPU60XX *);
static inline int32_t makeGyroDiv(const struct MPU60XX *);
static inline int32_t makeGyroMul(void);
static void onBusEvent(void *);
static void onPinEvent(void *);
static void onTimerEvent(void *);
static bool startConfigUpdate(struct MPU60XX *, bool *);
static void startSampleRead(struct MPU60XX *);
static void startSampleRequest(struct MPU60XX *);
static void startSuspendSequence(struct MPU60XX *);

static enum Result mpuInit(void *, const void *);
static void mpuDeinit(void *);
/*----------------------------------------------------------------------------*/
const struct EntityClass * const MPU60XX = &(const struct EntityClass){
    .size = sizeof(struct MPU60XX),
    .init = mpuInit,
    .deinit = mpuDeinit
};
/*----------------------------------------------------------------------------*/
static void busInit(struct MPU60XX *sensor, bool read)
{
  /* Lock the interface */
  ifSetParam(sensor->bus, IF_ACQUIRE, NULL);

  ifSetParam(sensor->bus, IF_ZEROCOPY, NULL);
  ifSetCallback(sensor->bus, onBusEvent, sensor);

  if (sensor->rate)
    ifSetParam(sensor->bus, IF_RATE, &sensor->rate);

  if (pinValid(sensor->gpio))
  {
    /* SPI bus */
    ifSetParam(sensor->bus, IF_SPI_UNIDIRECTIONAL, NULL);
    pinReset(sensor->gpio);
  }
  else
  {
    /* I2C bus */
    ifSetParam(sensor->bus, IF_ADDRESS, &sensor->address);

    if (read)
      ifSetParam(sensor->bus, IF_I2C_REPEATED_START, NULL);

    /* Start bus watchdog */
    timerSetOverflow(sensor->timer, calcResetTimeout(sensor->timer));
    timerSetValue(sensor->timer, 0);
    timerEnable(sensor->timer);
  }
}
/*----------------------------------------------------------------------------*/
static inline uint32_t calcResetTimeout(const struct Timer *timer)
{
  static const uint32_t RESET_FREQ = 10;
  return (timerGetFrequency(timer) + RESET_FREQ - 1) / RESET_FREQ;
}
/*----------------------------------------------------------------------------*/
static void calcValues(struct MPU60XX *sensor)
{
  const uint16_t flags = atomicLoad(&sensor->flags);

  if (flags & (FLAG_THERMO_LOOP | FLAG_THERMO_SAMPLE))
  {
    const int16_t raw = fetchThermoSample(sensor);
    const int32_t result = (int32_t)raw * 256 / 340 + 9352;

    sensor->thermometer->onResultCallback(
        sensor->thermometer->callbackArgument, &result, sizeof(result));
  }

  if (flags & (FLAG_ACCEL_LOOP | FLAG_ACCEL_SAMPLE))
  {
    const int32_t mul = makeAccelMul(sensor);

    int16_t raw[3];
    int32_t result[3];

    fetchAccelSample(sensor, raw);
    result[0] = (int32_t)raw[0] * mul;
    result[1] = (int32_t)raw[1] * mul;
    result[2] = (int32_t)raw[2] * mul;

    sensor->accelerometer->onResultCallback(
        sensor->accelerometer->callbackArgument, result, sizeof(result));
  }

  if (flags & (FLAG_GYRO_LOOP | FLAG_GYRO_SAMPLE))
  {
    const int32_t div = makeGyroDiv(sensor);
    const int32_t mul = makeGyroMul();

    int16_t raw[3];
    int32_t result[3];

    fetchGyroSample(sensor, raw);
    result[0] = (int32_t)raw[0] * mul / div;
    result[1] = (int32_t)raw[1] * mul / div;
    result[2] = (int32_t)raw[2] * mul / div;

    sensor->gyroscope->onResultCallback(
        sensor->gyroscope->callbackArgument, result, sizeof(result));
  }
}
/*----------------------------------------------------------------------------*/
static void fetchAccelSample(const struct MPU60XX *sensor, int16_t *result)
{
  const uint8_t * const buffer = sensor->buffer;

  result[0] = (int16_t)((buffer[0] << 8) | buffer[1]);
  result[1] = (int16_t)((buffer[2] << 8) | buffer[3]);
  result[2] = (int16_t)((buffer[4] << 8) | buffer[5]);
}
/*----------------------------------------------------------------------------*/
static void fetchGyroSample(const struct MPU60XX *sensor, int16_t *result)
{
  const uint8_t * const buffer = sensor->buffer;

  result[0] = (int16_t)((buffer[8] << 8) | buffer[9]);
  result[1] = (int16_t)((buffer[10] << 8) | buffer[11]);
  result[2] = (int16_t)((buffer[12] << 8) | buffer[13]);
}
/*----------------------------------------------------------------------------*/
static int16_t fetchThermoSample(const struct MPU60XX *sensor)
{
  const uint8_t * const buffer = sensor->buffer;
  const int16_t result = (int16_t)((buffer[6] << 8) | buffer[7]);

  return result;
}
/*----------------------------------------------------------------------------*/
static inline uint8_t makeAccelConfig(const struct MPU60XX *sensor)
{
  return ACCEL_CONFIG_AFS_SEL(sensor->accelScale - 1);
}
/*----------------------------------------------------------------------------*/
static inline int32_t makeAccelMul(const struct MPU60XX *sensor)
{
  return 256 >> (sensor->accelScale - 1);
}
/*----------------------------------------------------------------------------*/
static inline uint8_t makeGyroConfig(const struct MPU60XX *sensor)
{
  return GYRO_CONFIG_FS_SEL(sensor->gyroScale - 1);
}
/*----------------------------------------------------------------------------*/
static inline int32_t makeGyroDiv(const struct MPU60XX *sensor)
{
  return 4096 >> (sensor->gyroScale - 1);
}
/*----------------------------------------------------------------------------*/
static inline int32_t makeGyroMul(void)
{
  return 35744;
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct MPU60XX * const sensor = object;
  struct MPU60XXProxy * const proxy = sensor->active;
  bool release = true;

  timerDisable(sensor->timer);

  if (!pinValid(sensor->gpio)
      && ifGetParam(sensor->bus, IF_STATUS, NULL) != E_OK)
  {
    /* I2C bus */
    sensor->state = STATE_ERROR_WAIT;

    timerSetOverflow(sensor->timer, calcResetTimeout(sensor->timer));
    timerSetValue(sensor->timer, 0);
    timerEnable(sensor->timer);
  }

  switch (sensor->state)
  {
    case STATE_CONFIG_REQUEST_WAIT:
      release = false;
      /* Falls through */
    case STATE_CONFIG_BUS_WAIT:
      sensor->state = STATE_CONFIG_END;
      break;

    case STATE_SUSPEND_BUS_WAIT:
      sensor->state = STATE_SUSPEND_END;
      break;

    case STATE_REQUEST_WAIT:
      sensor->state = STATE_READ;
      release = false;
      break;

    case STATE_READ_WAIT:
      sensor->state = STATE_PROCESS;
      break;

    default:
      break;
  }

  if (release)
  {
    if (pinValid(sensor->gpio))
      pinSet(sensor->gpio);

    ifSetCallback(sensor->bus, NULL, NULL);
    ifSetParam(sensor->bus, IF_RELEASE, NULL);
  }

  proxy->onUpdateCallback(proxy->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void onPinEvent(void *object)
{
  struct MPU60XX * const sensor = object;
  struct MPU60XXProxy * const proxy = sensor->active;

  atomicFetchOr(&sensor->flags, FLAG_EVENT);
  proxy->onUpdateCallback(proxy->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct MPU60XX * const sensor = object;
  struct MPU60XXProxy * const proxy = sensor->active;

  switch (sensor->state)
  {
    case STATE_CONFIG_TIMER_WAIT:
      sensor->state = STATE_CONFIG_END;
      break;

    case STATE_ERROR_WAIT:
      sensor->state = STATE_ERROR_INTERFACE;
      break;

    default:
      if (pinValid(sensor->gpio))
        pinSet(sensor->gpio);

      ifSetCallback(sensor->bus, NULL, NULL);
      ifSetParam(sensor->bus, IF_RELEASE, NULL);
      sensor->state = STATE_ERROR_TIMEOUT;
      break;
  }

  proxy->onUpdateCallback(proxy->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static bool startConfigUpdate(struct MPU60XX *sensor, bool *busy)
{
  uint32_t timeout = 0;
  bool error = false;
  bool read = false;
  bool response = false;

  switch (sensor->step)
  {
    case CONFIG_RESET_WAIT:
    case CONFIG_STARTUP_WAIT:
    case CONFIG_READY_WAIT:
      timeout = calcResetTimeout(sensor->timer);
      break;

    case CONFIG_WHO_AM_I_READ:
    case CONFIG_PWR_MGMT_READ:
      response = true;
      break;

    case CONFIG_PWR_MGMT_RESET:
      sensor->buffer[0] = REG_PWR_MGMT_1;
      sensor->buffer[1] = PWR_MGMT_1_DEVICE_RESET;
      break;

    case CONFIG_WHO_AM_I_REQUEST:
      read = true;
      sensor->buffer[0] = REG_WHO_AM_I;
      break;

    case CONFIG_PWR_MGMT_REQUEST:
      /* Check REG_WHO_AM_I value */
      if (sensor->buffer[0] != WHO_AM_I_MPU60XX_VALUE)
      {
        error = true;
        break;
      }

      read = true;
      sensor->buffer[0] = REG_PWR_MGMT_1;
      break;

    case CONFIG_PWR_MGMT_SLEEP:
      /* Check REG_PWR_MGMT_1 value */
      if (sensor->buffer[0] != PWR_MGMT_1_SLEEP)
      {
        error = true;
        break;
      }

      sensor->buffer[0] = REG_PWR_MGMT_1;
      sensor->buffer[1] = PWR_MGMT_1_CLKSEL(CLKSEL_XG) | PWR_MGMT_1_SLEEP;
      break;

    case CONFIG_SIGNAL_PATH_RESET:
      sensor->buffer[0] = REG_SIGNAL_PATH_RESET;
      sensor->buffer[1] = SIGNAL_PATH_RESET_TEMP_RESET
          | SIGNAL_PATH_RESET_ACCEL_RESET
          | SIGNAL_PATH_RESET_GYRO_RESET;
      break;

    case CONFIG_USER_CTRL:
      sensor->buffer[0] = REG_USER_CTRL;
      sensor->buffer[1] = USER_CTRL_SIG_COND_RESET
          | (pinValid(sensor->gpio) ? USER_CTRL_I2C_IF_DIS : 0);
      break;

    case CONFIG_PWR_MGMT_WAKEUP:
      sensor->buffer[0] = REG_PWR_MGMT_1;
      sensor->buffer[1] = PWR_MGMT_1_CLKSEL(CLKSEL_XG);
      break;

    case CONFIG_GYRO:
      sensor->buffer[0] = REG_GYRO_CONFIG;
      sensor->buffer[1] = makeGyroConfig(sensor);
      break;

    case CONFIG_ACCEL:
      sensor->buffer[0] = REG_ACCEL_CONFIG;
      sensor->buffer[1] = makeAccelConfig(sensor);
      break;

    case CONFIG_BANDWIDTH:
      /* Bandwidth should be configured after clearing sleep bit */
      sensor->buffer[0] = REG_CONFIG;
      sensor->buffer[1] = CONFIG_DLPF_CFG(DLPF_CFG_ACCEL_184_GYRO_188);
      break;

    case CONFIG_RATE:
      /* Sample rate should be configured after clearing sleep bit */
      sensor->buffer[0] = REG_SMPLRT_DIV;
      sensor->buffer[1] = 1000 / sensor->sampleRate - 1;
      break;

    case CONFIG_INT_PIN:
      sensor->buffer[0] = REG_INT_PIN_CFG;
      sensor->buffer[1] = 0;
      break;

    case CONFIG_INT_ENABLE:
      sensor->buffer[0] = REG_INT_ENABLE;
      sensor->buffer[1] = INT_ENABLE_DATA_RDY_EN;
      break;

    default:
      return false;
  }

  if (error)
  {
    sensor->state = STATE_ERROR_DEVICE;
  }
  else if (timeout)
  {
    sensor->state = STATE_CONFIG_TIMER_WAIT;

    timerSetOverflow(sensor->timer, timeout);
    timerSetValue(sensor->timer, 0);
    timerEnable(sensor->timer);
  }
  else
  {

    if (response)
    {
      sensor->state = STATE_CONFIG_BUS_WAIT;
      ifRead(sensor->bus, sensor->buffer, 1);
    }
    else
    {
      sensor->state = read ? STATE_CONFIG_REQUEST_WAIT : STATE_CONFIG_BUS_WAIT;
      busInit(sensor, read);

      if (pinValid(sensor->gpio) && read)
      {
        /* Add read bit in case of SPI interface */
        sensor->buffer[0] |= 0x80;
      }

      ifWrite(sensor->bus, sensor->buffer, read ? 1 : 2);
    }

    *busy = true;
    return false;
  }

  *busy = false;
  return error;
}
/*----------------------------------------------------------------------------*/
static void startSampleRead(struct MPU60XX *sensor)
{
  ifRead(sensor->bus, sensor->buffer, sizeof(sensor->buffer));
}
/*----------------------------------------------------------------------------*/
static void startSampleRequest(struct MPU60XX *sensor)
{
  sensor->buffer[0] = REG_ACCEL_XOUT_H;

  busInit(sensor, true);
  ifWrite(sensor->bus, sensor->buffer, 1);
}
/*----------------------------------------------------------------------------*/
static void startSuspendSequence(struct MPU60XX *sensor)
{
  sensor->buffer[0] = REG_PWR_MGMT_1;
  sensor->buffer[1] = PWR_MGMT_1_CLKSEL(CLKSEL_STOP) | PWR_MGMT_1_SLEEP;

  busInit(sensor, false);
  ifWrite(sensor->bus, sensor->buffer, 2);
}
/*----------------------------------------------------------------------------*/
static enum Result mpuInit(void *object, const void *configBase)
{
  const struct MPU60XXConfig * const config = configBase;
  assert(config != NULL);
  assert(config->bus != NULL);
  assert(config->event != NULL);
  assert(config->timer != NULL);

  struct MPU60XX * const sensor = object;

  sensor->active = NULL;
  sensor->accelerometer = NULL;
  sensor->gyroscope = NULL;
  sensor->thermometer = NULL;

  sensor->bus = config->bus;
  sensor->event = config->event;
  sensor->timer = config->timer;
  sensor->rate = config->rate;

  sensor->flags = 0;
  sensor->state = STATE_IDLE;
  sensor->step = CONFIG_BEGIN;

  // TODO Bandwidth setup, DLPF settings

  /* Scale and rate settings */

  if (config->sampleRate > 0)
    sensor->sampleRate = config->sampleRate;
  else
    return E_VALUE;

  if (config->accelScale != MPU60XX_ACCEL_DEFAULT)
    sensor->accelScale = config->accelScale;
  else
    sensor->accelScale = MPU60XX_ACCEL_16;

  if (config->gyroScale != MPU60XX_GYRO_DEFAULT)
    sensor->gyroScale = config->gyroScale;
  else
    sensor->gyroScale = MPU60XX_GYRO_2000;

  /* Peripheral interface configuration */

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
    sensor->address = config->address;
    sensor->gpio = pinStub();
  }

  interruptSetCallback(sensor->event, onPinEvent, sensor);
  timerSetAutostop(sensor->timer, true);
  timerSetCallback(sensor->timer, onTimerEvent, sensor);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void mpuDeinit(void *object)
{
  struct MPU60XX * const sensor = object;

  timerDisable(sensor->timer);
  timerSetCallback(sensor->timer, NULL, NULL);

  interruptDisable(sensor->event);
  interruptSetCallback(sensor->event, NULL, NULL);

  if (sensor->accelerometer != NULL)
    deinit(sensor->accelerometer);

  if (sensor->gyroscope != NULL)
    deinit(sensor->gyroscope);

  if (sensor->thermometer != NULL)
    deinit(sensor->thermometer);
}
/*----------------------------------------------------------------------------*/
enum SensorStatus mpu60xxGetStatus(const struct MPU60XX *sensor)
{
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
void mpu60xxReset(struct MPU60XX *sensor)
{
  struct MPU60XXProxy * const proxy = sensor->active;
  assert(proxy != NULL);

  atomicFetchOr(&sensor->flags, FLAG_RESET);
  proxy->onUpdateCallback(proxy->callbackArgument);
}
/*----------------------------------------------------------------------------*/
void mpu60xxSample(struct MPU60XX *sensor)
{
  struct MPU60XXProxy * const proxy = sensor->active;
  assert(proxy != NULL);

  proxy->onUpdateCallback(proxy->callbackArgument);
}
/*----------------------------------------------------------------------------*/
void mpu60xxStart(struct MPU60XX *sensor)
{
  struct MPU60XXProxy * const proxy = sensor->active;
  assert(proxy != NULL);

  proxy->onUpdateCallback(proxy->callbackArgument);
}
/*----------------------------------------------------------------------------*/
void mpu60xxStop(struct MPU60XX *sensor)
{
  struct MPU60XXProxy * const proxy = sensor->active;
  assert(proxy != NULL);

  proxy->onUpdateCallback(proxy->callbackArgument);
}
/*----------------------------------------------------------------------------*/
void mpu60xxSuspend(struct MPU60XX *sensor)
{
  struct MPU60XXProxy * const proxy = sensor->active;
  assert(proxy != NULL);

  atomicFetchOr(&sensor->flags, FLAG_SUSPEND);
  proxy->onUpdateCallback(proxy->callbackArgument);
}
/*----------------------------------------------------------------------------*/
bool mpu60xxUpdate(struct MPU60XX *sensor)
{
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
        const uint16_t flags = atomicLoad(&sensor->flags);

        if (flags & FLAG_RESET)
        {
          sensor->state = STATE_CONFIG_START;
          updated = true;
        }
        else if (flags & FLAG_SUSPEND)
        {
          sensor->state = STATE_SUSPEND_START;
          updated = true;
        }
        else if (flags & (FLAG_LOOP | FLAG_SAMPLE))
        {
          if (flags & FLAG_READY)
          {
            if (flags & FLAG_LOOP)
            {
              sensor->state = STATE_EVENT_WAIT;
              interruptEnable(sensor->event);
            }
            else
            {
              sensor->state = STATE_REQUEST;
              updated = true;
            }
          }
          else
            interruptDisable(sensor->event);
        }
        else
          interruptDisable(sensor->event);

        break;
      }

      case STATE_CONFIG_START:
        interruptDisable(sensor->event);

        sensor->state = STATE_CONFIG_UPDATE;
        sensor->step = CONFIG_BEGIN;
        atomicFetchAnd(&sensor->flags, ~FLAG_READY);

        updated = true;
        break;

      case STATE_CONFIG_UPDATE:
        updated = startConfigUpdate(sensor, &busy);
        break;

      case STATE_CONFIG_TIMER_WAIT:
        break;

      case STATE_CONFIG_REQUEST_WAIT:
      case STATE_CONFIG_BUS_WAIT:
        busy = true;
        break;

      case STATE_CONFIG_END:
        if (++sensor->step == CONFIG_END)
        {
          sensor->state = STATE_IDLE;

          atomicFetchAnd(&sensor->flags, ~(FLAG_RESET | FLAG_EVENT));
          atomicFetchOr(&sensor->flags, FLAG_READY);
        }
        else
        {
          sensor->state = STATE_CONFIG_UPDATE;
        }

        updated = true;
        break;

      case STATE_SUSPEND_START:
        interruptDisable(sensor->event);

        sensor->state = STATE_SUSPEND_BUS_WAIT;
        startSuspendSequence(sensor);
        busy = true;
        break;

      case STATE_SUSPEND_BUS_WAIT:
        busy = true;
        break;

      case STATE_SUSPEND_END:
        sensor->state = STATE_IDLE;

        /* Clear all flags except reset flag */
        atomicFetchAnd(&sensor->flags, FLAG_RESET);
        updated = true;
        break;

      case STATE_EVENT_WAIT:
      {
        const uint16_t flags = atomicLoad(&sensor->flags);

        if (flags & (FLAG_RESET | FLAG_EVENT))
        {
          if (flags & FLAG_RESET)
          {
            sensor->state = STATE_CONFIG_START;
          }
          else if (flags & FLAG_SUSPEND)
          {
            sensor->state = STATE_SUSPEND_START;
          }
          else
          {
            sensor->state = STATE_REQUEST;
            atomicFetchAnd(&sensor->flags, ~FLAG_EVENT);
          }

          updated = true;
        }
        else if (!(flags & FLAG_LOOP))
        {
          sensor->state = STATE_IDLE;
          updated = true;
        }
        break;
      }

      case STATE_REQUEST:
        sensor->state = STATE_REQUEST_WAIT;
        startSampleRequest(sensor);
        busy = true;
        break;

      case STATE_REQUEST_WAIT:
        busy = true;
        break;

      case STATE_READ:
        sensor->state = STATE_READ_WAIT;
        startSampleRead(sensor);
        busy = true;
        break;

      case STATE_READ_WAIT:
        busy = true;
        break;

      case STATE_PROCESS:
        calcValues(sensor);

        sensor->state = STATE_IDLE;
        atomicFetchAnd(&sensor->flags, ~FLAG_SAMPLE);

        updated = true;
        break;

      case STATE_ERROR_WAIT:
        break;

      case STATE_ERROR_DEVICE:
      case STATE_ERROR_INTERFACE:
      case STATE_ERROR_TIMEOUT:
        if (sensor->active->onErrorCallback != NULL)
        {
          enum SensorResult result;

          if (sensor->state == STATE_ERROR_DEVICE)
            result = SENSOR_CALIBRATION_ERROR;
          else if (sensor->state == STATE_ERROR_INTERFACE)
            result = SENSOR_INTERFACE_ERROR;
          else
            result = SENSOR_INTERFACE_TIMEOUT;

          sensor->active->onErrorCallback(sensor->active->callbackArgument,
              result);
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
struct MPU60XXProxy *mpu60xxMakeAccelerometer(struct MPU60XX *sensor)
{
  if (sensor->accelerometer == NULL)
  {
    const struct MPU60XXProxyConfig config = {
        .parent = sensor
    };

    sensor->accelerometer = init(MPU60XXAccelerometer, &config);
    if (sensor->active == NULL)
      sensor->active = sensor->accelerometer;
  }

  return sensor->accelerometer;
}
/*----------------------------------------------------------------------------*/
struct MPU60XXProxy *mpu60xxMakeGyroscope(struct MPU60XX *sensor)
{
  if (sensor->gyroscope == NULL)
  {
    const struct MPU60XXProxyConfig config = {
        .parent = sensor
    };

    sensor->gyroscope = init(MPU60XXGyroscope, &config);
    if (sensor->active == NULL)
      sensor->active = sensor->gyroscope;
  }

  return sensor->gyroscope;
}
/*----------------------------------------------------------------------------*/
struct MPU60XXProxy *mpu60xxMakeThermometer(struct MPU60XX *sensor)
{
  if (sensor->thermometer == NULL)
  {
    const struct MPU60XXProxyConfig config = {
        .parent = sensor
    };

    sensor->thermometer = init(MPU60XXThermometer, &config);
    if (sensor->active == NULL)
      sensor->active = sensor->thermometer;
  }

  return sensor->thermometer;
}
