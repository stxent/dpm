/*
 * m24.c
 * Copyright (C) 2019, 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/memory/m24.h>
#include <halm/generic/i2c.h>
#include <halm/timer.h>
#include <halm/wq.h>
#include <xcore/accel.h>
#include <xcore/asm.h>
#include <xcore/bits.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
#define WRITE_CYCLE_TIME 5

enum
{
  STATE_IDLE,

  STATE_READ_SETUP,
  STATE_READ_SETUP_WAIT,
  STATE_READ_DATA,
  STATE_READ_DATA_WAIT,
  STATE_WRITE_DATA,
  STATE_WRITE_DATA_WAIT,
  STATE_WRITE_PROGRAM,
  STATE_WRITE_PROGRAM_WAIT,

  STATE_ERROR_WAIT,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
};

enum
{
  STATUS_DONE,
  STATUS_BUSY,
  STATUS_ERROR_INTERFACE,
  STATUS_ERROR_TIMEOUT
} __attribute__((packed));
/*----------------------------------------------------------------------------*/
static void fillDataAddress(uint8_t *, const struct M24 *, uint32_t);
static uint32_t makeSlaveAddress(const struct M24 *, uint32_t);

static void busInit(struct M24 *, uint32_t, bool);
static void invokeUpdate(struct M24 *);
static void onBusEvent(void *);
static void onTimerEvent(void *);
static void startBusTimeout(struct Timer *);
static void startProgramTimeout(struct Timer *, uint32_t);
static void updateTask(void *);
/*----------------------------------------------------------------------------*/
static enum Result memoryInitEeprom(void *, const void *);
static enum Result memoryInitFram(void *, const void *);
static enum Result memoryInitGeneric(void *, const void *, uint32_t);
static void memoryDeinit(void *);
static void memorySetCallback(void *, void (*)(void *), void *);
static enum Result memoryGetParam(void *, int, void *);
static enum Result memorySetParam(void *, int, const void *);
static size_t memoryRead(void *, void *, size_t);
static size_t memoryWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const FM24 = &(const struct InterfaceClass){
    .size = sizeof(struct M24),
    .init = memoryInitFram,
    .deinit = memoryDeinit,

    .setCallback = memorySetCallback,
    .getParam = memoryGetParam,
    .setParam = memorySetParam,
    .read = memoryRead,
    .write = memoryWrite
};

const struct InterfaceClass * const M24 = &(const struct InterfaceClass){
    .size = sizeof(struct M24),
    .init = memoryInitEeprom,
    .deinit = memoryDeinit,

    .setCallback = memorySetCallback,
    .getParam = memoryGetParam,
    .setParam = memorySetParam,
    .read = memoryRead,
    .write = memoryWrite
};
/*----------------------------------------------------------------------------*/
static void fillDataAddress(uint8_t *buffer, const struct M24 *memory,
    uint32_t position)
{
  const uint32_t address = position & MASK(memory->shift);
  const size_t width = memory->width - 1;

  for (size_t i = 0; i <= width; ++i)
    buffer[i] = (uint8_t)(address >> ((width - i) * 8));
}
/*----------------------------------------------------------------------------*/
static uint32_t makeSlaveAddress(const struct M24 *memory, uint32_t position)
{
  const uint16_t block = position >> memory->shift;
  return memory->address | block;
}
/*----------------------------------------------------------------------------*/
static void busInit(struct M24 *memory, uint32_t position, bool read)
{
  const uint32_t address = memory->address ?
      makeSlaveAddress(memory, position) : 0;

  /* Lock the interface */
  ifSetParam(memory->bus, IF_ACQUIRE, NULL);

  ifSetParam(memory->bus, IF_ADDRESS, &address);
  ifSetParam(memory->bus, IF_ZEROCOPY, NULL);
  ifSetCallback(memory->bus, onBusEvent, memory);

  if (memory->rate)
    ifSetParam(memory->bus, IF_RATE, &memory->rate);

  if (read)
    ifSetParam(memory->bus, IF_I2C_REPEATED_START, NULL);

  /* Start bus watchdog */
  startBusTimeout(memory->timer);
}
/*----------------------------------------------------------------------------*/
static void invokeUpdate(struct M24 *memory)
{
  assert(memory->updateCallback != NULL || memory->wq != NULL);

  if (memory->updateCallback != NULL)
  {
    memory->updateCallback(memory->updateCallbackArgument);
  }
  else if (!memory->pending)
  {
    memory->pending = true;

    if (wqAdd(memory->wq, updateTask, memory) != E_OK)
      memory->pending = false;
  }
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct M24 * const memory = object;
  bool busy = false;

  timerDisable(memory->timer);

  if (ifGetParam(memory->bus, IF_STATUS, NULL) != E_OK)
  {
    memory->transfer.state = STATE_ERROR_WAIT;

    /* Start bus timeout sequence */
    startBusTimeout(memory->timer);
  }

  switch (memory->transfer.state)
  {
    case STATE_READ_SETUP_WAIT:
      busy = true;
      memory->transfer.state = STATE_READ_DATA;
      break;

    case STATE_READ_DATA_WAIT:
      memory->transfer.count -= memory->transfer.chunk;
      memory->transfer.position += memory->transfer.chunk;
      memory->transfer.rxBuffer += memory->transfer.chunk;
      memory->transfer.state = STATE_READ_SETUP;
      break;

    case STATE_WRITE_DATA_WAIT:
      memory->transfer.count -= memory->transfer.chunk;
      memory->transfer.position += memory->transfer.chunk;
      memory->transfer.txBuffer += memory->transfer.chunk;

      if (memory->delay)
        memory->transfer.state = STATE_WRITE_PROGRAM;
      else
        memory->transfer.state = STATE_WRITE_DATA;
      break;

    default:
      break;
  }

  if (!busy)
  {
    ifSetCallback(memory->bus, NULL, NULL);
    ifSetParam(memory->bus, IF_RELEASE, NULL);
  }

  invokeUpdate(memory);
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct M24 * const memory = object;

  switch (memory->transfer.state)
  {
    case STATE_WRITE_PROGRAM_WAIT:
      memory->transfer.state = STATE_WRITE_DATA;
      break;

    case STATE_ERROR_WAIT:
      memory->transfer.state = STATE_ERROR_INTERFACE;
      break;

    default:
      ifSetCallback(memory->bus, NULL, NULL);
      ifSetParam(memory->bus, IF_RELEASE, NULL);
      memory->transfer.state = STATE_ERROR_TIMEOUT;
      break;
  }

  invokeUpdate(memory);
}
/*----------------------------------------------------------------------------*/
static void startBusTimeout(struct Timer *timer)
{
  timerSetOverflow(timer, timerGetFrequency(timer) / 10);
  timerSetValue(timer, 0);
  timerEnable(timer);
}
/*----------------------------------------------------------------------------*/
static void startProgramTimeout(struct Timer *timer, uint32_t delay)
{
  timerSetOverflow(timer, delay);
  timerSetValue(timer, 0);
  timerEnable(timer);
}
/*----------------------------------------------------------------------------*/
static void updateTask(void *argument)
{
  struct M24 * const memory = argument;

  memory->pending = false;
  m24Update(memory);
}
/*----------------------------------------------------------------------------*/
static enum Result memoryInitEeprom(void *object, const void *configBase)
{
  return memoryInitGeneric(object, configBase, WRITE_CYCLE_TIME);
}
/*----------------------------------------------------------------------------*/
static enum Result memoryInitFram(void *object, const void *configBase)
{
  return memoryInitGeneric(object, configBase, 0);
}
/*----------------------------------------------------------------------------*/
static enum Result memoryInitGeneric(void *object, const void *configBase,
    uint32_t delay)
{
  const struct M24Config * const config = configBase;
  assert(config != NULL);
  assert(config->bus != NULL && config->timer != NULL);
  assert(config->blocks && config->chipSize && config->pageSize);
  assert((config->chipSize & (config->chipSize - 1)) == 0);
  assert((config->pageSize & (config->pageSize - 1)) == 0);

  struct M24 * const memory = object;

  memory->callback = NULL;
  memory->errorCallback = NULL;
  memory->idleCallback = NULL;
  memory->updateCallback = NULL;

  memory->bus = config->bus;
  memory->timer = config->timer;
  memory->wq = NULL;
  memory->blocking = true;
  memory->pending = false;

  memory->address = config->address;
  memory->rate = config->rate;

  memory->chipSize = config->chipSize;
  memory->pageSize = config->pageSize;

  if (delay)
  {
    const uint32_t frequency = timerGetFrequency(config->timer);
    const uint64_t timeout = (delay * (1ULL << 32)) / 1000;
    const uint32_t overflow = (frequency * timeout + ((1ULL << 32) - 1)) >> 32;

    memory->delay = overflow;
  }
  else
    memory->delay = 0;

  const uint32_t width =
      31 - countLeadingZeros32(config->chipSize / config->blocks);

  memory->shift = width;
  memory->width = (width + 7) >> 3;

  memory->transfer.buffer = malloc(config->pageSize + memory->width);
  if (memory->transfer.buffer == NULL)
    return E_MEMORY;

  memory->transfer.rxBuffer = NULL;
  memory->transfer.txBuffer = NULL;
  memory->transfer.chunk = 0;
  memory->transfer.count = 0;
  memory->transfer.position = 0;
  memory->transfer.state = STATE_IDLE;
  memory->transfer.status = STATUS_DONE;

  timerSetAutostop(memory->timer, true);
  timerSetCallback(memory->timer, onTimerEvent, memory);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void memoryDeinit(void *object)
{
  struct M24 * const memory = object;
  free(memory->transfer.buffer);
}
/*----------------------------------------------------------------------------*/
static void memorySetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct M24 * const memory = object;

  memory->callbackArgument = argument;
  memory->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result memoryGetParam(void *object, int parameter, void *data)
{
  struct M24 * const memory = object;

  switch ((enum IfParameter)parameter)
  {
    case IF_POSITION:
      *(uint32_t *)data = memory->transfer.position;
      return E_OK;

    case IF_SIZE:
      *(uint32_t *)data = memory->chipSize;
      return E_OK;

    case IF_STATUS:
    {
      switch (memory->transfer.status)
      {
        case STATUS_BUSY:
          return E_BUSY;

        case STATUS_ERROR_INTERFACE:
          return E_TIMEOUT;

        case STATUS_ERROR_TIMEOUT:
          return E_INTERFACE;

        default:
          return E_OK;
      }
    }

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result memorySetParam(void *object, int parameter, const void *data)
{
  struct M24 * const memory = object;

  switch ((enum IfParameter)parameter)
  {
    case IF_POSITION:
    {
      const uint32_t position = *(const uint32_t *)data;

      if (position < memory->chipSize)
      {
        memory->transfer.position = position;
        return E_OK;
      }
      else
        return E_ADDRESS;
    }

    case IF_BLOCKING:
      memory->blocking = true;
      return E_OK;

    case IF_ZEROCOPY:
      memory->blocking = false;
      return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static size_t memoryRead(void *object, void *buffer, size_t length)
{
  struct M24 * const memory = object;

  if (length)
  {
    if (memory->transfer.count > 0)
    {
      /* Interface is busy */
      return 0;
    }

    memory->transfer.rxBuffer = buffer;
    memory->transfer.count = length;
    invokeUpdate(memory);

    if (memory->blocking)
    {
      while (memory->transfer.state != STATE_IDLE)
        barrier();

      return memory->transfer.status == STATUS_DONE ? length : 0;
    }
  }

  return length;
}
/*----------------------------------------------------------------------------*/
static size_t memoryWrite(void *object, const void *buffer, size_t length)
{
  struct M24 * const memory = object;

  if (length)
  {
    if (memory->transfer.count > 0)
    {
      /* Interface is busy */
      return 0;
    }

    memory->transfer.txBuffer = buffer;
    memory->transfer.count = length;
    invokeUpdate(memory);

    if (memory->blocking)
    {
      while (memory->transfer.state != STATE_IDLE)
        barrier();

      return memory->transfer.status == STATUS_DONE ? length : 0;
    }
  }

  return length;
}
/*----------------------------------------------------------------------------*/
void m24SetErrorCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct M24 * const memory = object;

  assert(callback != NULL);

  memory->errorCallbackArgument = argument;
  memory->errorCallback = callback;
}
/*----------------------------------------------------------------------------*/
void m24SetIdleCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct M24 * const memory = object;

  assert(callback != NULL);

  memory->idleCallbackArgument = argument;
  memory->idleCallback = callback;
}
/*----------------------------------------------------------------------------*/
void m24SetUpdateCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct M24 * const memory = object;

  assert(callback != NULL);
  assert(memory->wq == NULL);

  memory->updateCallbackArgument = argument;
  memory->updateCallback = callback;
}
/*----------------------------------------------------------------------------*/
void m24SetUpdateWorkQueue(void *object, struct WorkQueue *wq)
{
  struct M24 * const memory = object;

  assert(wq != NULL);
  assert(memory->updateCallback == NULL);

  memory->wq = wq;
}
/*----------------------------------------------------------------------------*/
bool m24Update(void *object)
{
  struct M24 * const memory = object;
  bool busy;
  bool updated;

  do
  {
    busy = false;
    updated = false;

    switch (memory->transfer.state)
    {
      case STATE_IDLE:
        if (memory->transfer.rxBuffer != NULL)
        {
          memory->transfer.status = STATUS_BUSY;
          memory->transfer.state = STATE_READ_SETUP;
          updated = true;
        }
        else if (memory->transfer.txBuffer != NULL)
        {
          memory->transfer.status = STATUS_BUSY;
          memory->transfer.state = STATE_WRITE_DATA;
          updated = true;
        }
        break;

      case STATE_READ_SETUP:
        if (memory->transfer.count)
        {
          busy = true;
          memory->transfer.state = STATE_READ_SETUP_WAIT;

          const uint32_t begin = memory->transfer.position;
          const uint32_t nextPagePosition =
              ((begin / memory->pageSize) + 1) * memory->pageSize;

          memory->transfer.chunk = MIN(nextPagePosition - begin,
              memory->transfer.count);

          fillDataAddress(memory->transfer.buffer, memory,
              memory->transfer.position);

          busInit(memory, memory->transfer.position, true);
          ifWrite(memory->bus, memory->transfer.buffer, memory->width);
        }
        else
        {
          memory->transfer.rxBuffer = NULL;
          memory->transfer.status = STATUS_DONE;
          memory->transfer.state = STATE_IDLE;

          /* Idle callback for Bus Handlers */
          if (memory->idleCallback != NULL)
            memory->idleCallback(memory->idleCallbackArgument);
          /* User callback for Interface class */
          if (memory->callback != NULL)
            memory->callback(memory->callbackArgument);
        }
        break;

      case STATE_READ_DATA:
        busy = true;
        memory->transfer.state = STATE_READ_DATA_WAIT;

        ifRead(memory->bus, memory->transfer.rxBuffer, memory->transfer.chunk);
        break;

      case STATE_WRITE_DATA:
        if (memory->transfer.count)
        {
          busy = true;
          memory->transfer.state = STATE_WRITE_DATA_WAIT;

          const uint32_t begin = memory->transfer.position;
          const uint32_t nextPagePosition =
              ((begin / memory->pageSize) + 1) * memory->pageSize;

          memory->transfer.chunk = MIN(nextPagePosition - begin,
              memory->transfer.count);

          fillDataAddress(memory->transfer.buffer, memory,
              memory->transfer.position);
          memcpy(memory->transfer.buffer + memory->width,
              memory->transfer.txBuffer, memory->transfer.chunk);

          busInit(memory, memory->transfer.position, false);
          ifWrite(memory->bus, memory->transfer.buffer,
              memory->width + memory->transfer.chunk);
        }
        else
        {
          memory->transfer.txBuffer = NULL;
          memory->transfer.status = STATUS_DONE;
          memory->transfer.state = STATE_IDLE;

          /* Idle callback for Bus Handlers */
          if (memory->idleCallback != NULL)
            memory->idleCallback(memory->idleCallbackArgument);
          /* User callback for Interface class */
          if (memory->callback != NULL)
            memory->callback(memory->callbackArgument);
        }
        break;

      case STATE_WRITE_PROGRAM:
        memory->transfer.state = STATE_WRITE_PROGRAM_WAIT;
        startProgramTimeout(memory->timer, memory->delay);
        break;

      case STATE_ERROR_INTERFACE:
      case STATE_ERROR_TIMEOUT:
        memory->transfer.count = 0;
        memory->transfer.rxBuffer = NULL;
        memory->transfer.txBuffer = NULL;

        memory->transfer.status =
            (memory->transfer.state == STATE_ERROR_INTERFACE) ?
                STATUS_ERROR_INTERFACE : STATUS_ERROR_TIMEOUT;
        memory->transfer.state = STATE_IDLE;

        /* Error callback for Bus Handlers */
        if (memory->errorCallback != NULL)
          memory->errorCallback(memory->errorCallbackArgument);
        /* User callback for Interface class */
        if (memory->callback != NULL)
          memory->callback(memory->callbackArgument);

        updated = true;
        break;

      default:
        break;
    }
  }
  while (updated);

  return busy;
}
