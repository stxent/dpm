/*
 * mpu60xx_proxy.c
 * Copyright (C) 2022 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/sensors/mpu60xx.h>
#include <dpm/sensors/mpu60xx_defs.h>
#include <xcore/atomic.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
enum
{
  PROXY_TYPE_ACCEL,
  PROXY_TYPE_GYRO,
  PROXY_TYPE_THERMO
};
/*----------------------------------------------------------------------------*/
static void baseProxyInit(struct MPU60XXProxy *,
    const struct MPU60XXProxyConfig *);
static enum Result accelProxyInit(void *, const void *);
static enum Result gyroProxyInit(void *, const void *);
static enum Result thermoProxyInit(void *, const void *);
static inline uint16_t typeToLoopConstant(const struct MPU60XXProxy *);
static inline uint16_t typeToSampleConstant(const struct MPU60XXProxy *);

static void proxyDeinit(void *);
static const char *proxyGetFormat(const void *);
static enum SensorStatus proxyGetStatus(const void *);
static void proxySetCallbackArgument(void *, void *);
static void proxySetErrorCallback(void *, void (*)(void *, enum SensorResult));
static void proxySetResultCallback(void *,
    void (*)(void *, const void *, size_t));
static void proxySetUpdateCallback(void *, void (*)(void *));
static void proxyReset(void *);
static void proxySample(void *);
static void proxyStart(void *);
static void proxyStop(void *);
static void proxySuspend(void *);
static bool proxyUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct SensorClass * const MPU60XXAccelerometer =
    &(const struct SensorClass){
    .size = sizeof(struct MPU60XXProxy),
    .init = accelProxyInit,
    .deinit = proxyDeinit,

    .getFormat = proxyGetFormat,
    .getStatus = proxyGetStatus,
    .setCallbackArgument = proxySetCallbackArgument,
    .setErrorCallback = proxySetErrorCallback,
    .setResultCallback = proxySetResultCallback,
    .setUpdateCallback = proxySetUpdateCallback,
    .reset = proxyReset,
    .sample = proxySample,
    .start = proxyStart,
    .stop = proxyStop,
    .suspend = proxySuspend,
    .update = proxyUpdate
};

const struct SensorClass * const MPU60XXGyroscope =
    &(const struct SensorClass){
    .size = sizeof(struct MPU60XXProxy),
    .init = gyroProxyInit,
    .deinit = proxyDeinit,

    .getFormat = proxyGetFormat,
    .getStatus = proxyGetStatus,
    .setCallbackArgument = proxySetCallbackArgument,
    .setErrorCallback = proxySetErrorCallback,
    .setResultCallback = proxySetResultCallback,
    .setUpdateCallback = proxySetUpdateCallback,
    .reset = proxyReset,
    .sample = proxySample,
    .start = proxyStart,
    .stop = proxyStop,
    .suspend = proxySuspend,
    .update = proxyUpdate
};

const struct SensorClass * const MPU60XXThermometer =
    &(const struct SensorClass){
    .size = sizeof(struct MPU60XXProxy),
    .init = thermoProxyInit,
    .deinit = proxyDeinit,

    .getFormat = proxyGetFormat,
    .getStatus = proxyGetStatus,
    .setCallbackArgument = proxySetCallbackArgument,
    .setErrorCallback = proxySetErrorCallback,
    .setResultCallback = proxySetResultCallback,
    .setUpdateCallback = proxySetUpdateCallback,
    .reset = proxyReset,
    .sample = proxySample,
    .start = proxyStart,
    .stop = proxyStop,
    .suspend = proxySuspend,
    .update = proxyUpdate
};
/*----------------------------------------------------------------------------*/
static void baseProxyInit(struct MPU60XXProxy *proxy,
    const struct MPU60XXProxyConfig *config)
{
  assert(config != NULL);
  assert(config->parent != NULL);

  proxy->callbackArgument = NULL;
  proxy->onErrorCallback = NULL;
  proxy->onResultCallback = NULL;
  proxy->onUpdateCallback = NULL;
  proxy->parent = config->parent;
}
/*----------------------------------------------------------------------------*/
static enum Result accelProxyInit(void *object, const void *configBase)
{
  const struct MPU60XXProxyConfig * const config = configBase;
  struct MPU60XXProxy * const proxy = object;

  baseProxyInit(proxy, config);
  proxy->type = PROXY_TYPE_ACCEL;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum Result gyroProxyInit(void *object, const void *configBase)
{
  const struct MPU60XXProxyConfig * const config = configBase;
  struct MPU60XXProxy * const proxy = object;

  baseProxyInit(proxy, config);
  proxy->type = PROXY_TYPE_GYRO;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum Result thermoProxyInit(void *object, const void *configBase)
{
  const struct MPU60XXProxyConfig * const config = configBase;
  struct MPU60XXProxy * const proxy = object;

  baseProxyInit(proxy, config);
  proxy->type = PROXY_TYPE_THERMO;

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static inline uint16_t typeToLoopConstant(const struct MPU60XXProxy *proxy)
{
  switch (proxy->type)
  {
    case PROXY_TYPE_ACCEL:
      return FLAG_ACCEL_LOOP;

    case PROXY_TYPE_GYRO:
      return FLAG_GYRO_LOOP;

    default:
      return FLAG_THERMO_LOOP;
  }
}
/*----------------------------------------------------------------------------*/
static inline uint16_t typeToSampleConstant(const struct MPU60XXProxy *proxy)
{
  switch (proxy->type)
  {
    case PROXY_TYPE_ACCEL:
      return FLAG_ACCEL_SAMPLE;

    case PROXY_TYPE_GYRO:
      return FLAG_GYRO_SAMPLE;

    default:
      return FLAG_THERMO_SAMPLE;
  }
}
/*----------------------------------------------------------------------------*/
static void proxyDeinit(void *object __attribute__((unused)))
{
}
/*----------------------------------------------------------------------------*/
static const char *proxyGetFormat(const void *object)
{
  const struct MPU60XXProxy * const proxy = object;
  return proxy->type == PROXY_TYPE_THERMO ? "i24q8" : "i16q16i16q16i16q16";
}
/*----------------------------------------------------------------------------*/
static enum SensorStatus proxyGetStatus(const void *object)
{
  const struct MPU60XXProxy * const proxy = object;
  return mpu60xxGetStatus(proxy->parent);
}
/*----------------------------------------------------------------------------*/
static void proxySetCallbackArgument(void *object, void *argument)
{
  struct MPU60XXProxy * const proxy = object;
  proxy->callbackArgument = argument;
}
/*----------------------------------------------------------------------------*/
static void proxySetErrorCallback(void *object,
    void (*callback)(void *, enum SensorResult))
{
  struct MPU60XXProxy * const proxy = object;
  proxy->onErrorCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void proxySetResultCallback(void *object,
    void (*callback)(void *, const void *, size_t))
{
  struct MPU60XXProxy * const proxy = object;
  proxy->onResultCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void proxySetUpdateCallback(void *object, void (*callback)(void *))
{
  struct MPU60XXProxy * const proxy = object;
  proxy->onUpdateCallback = callback;
}
/*----------------------------------------------------------------------------*/
static void proxyReset(void *object)
{
  struct MPU60XXProxy * const proxy = object;
  mpu60xxReset(proxy->parent);
}
/*----------------------------------------------------------------------------*/
static void proxySample(void *object)
{
  struct MPU60XXProxy * const proxy = object;

  assert(proxy->onResultCallback != NULL);
  assert(proxy->onUpdateCallback != NULL);

  atomicFetchOr(&proxy->parent->flags, typeToSampleConstant(proxy));
  mpu60xxSample(proxy->parent);
}
/*----------------------------------------------------------------------------*/
static void proxyStart(void *object)
{
  struct MPU60XXProxy * const proxy = object;

  assert(proxy->onResultCallback != NULL);
  assert(proxy->onUpdateCallback != NULL);

  atomicFetchOr(&proxy->parent->flags, typeToLoopConstant(proxy));
  mpu60xxStart(proxy->parent);
}
/*----------------------------------------------------------------------------*/
static void proxyStop(void *object)
{
  struct MPU60XXProxy * const proxy = object;

  atomicFetchAnd(&proxy->parent->flags,
      ~(typeToLoopConstant(proxy) | typeToSampleConstant(proxy)
          | (FLAG_RESET | FLAG_SUSPEND)));
  mpu60xxStop(proxy->parent);
}
/*----------------------------------------------------------------------------*/
static void proxySuspend(void *object)
{
  struct MPU60XXProxy * const proxy = object;
  mpu60xxSuspend(proxy->parent);
}
/*----------------------------------------------------------------------------*/
static bool proxyUpdate(void *object)
{
  struct MPU60XXProxy * const proxy = object;
  return mpu60xxUpdate(proxy->parent);
}
