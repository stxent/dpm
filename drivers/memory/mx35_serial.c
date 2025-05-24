/*
 * mx35_serial.c
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/memory/flash_defs.h>
#include <dpm/memory/mx35.h>
#include <dpm/memory/mx35_defs.h>
#include <dpm/memory/mx35_serial.h>
#include <halm/generic/flash.h>
#include <halm/generic/spi.h>
#include <halm/timer.h>
#include <xcore/memory.h>
#include <assert.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
#define DEFAULT_POLL_RATE 100

enum
{
  STATE_IDLE,
  STATE_READ_PAGE_START,
  STATE_READ_PAGE_CHECK,
  STATE_READ_PAGE_WAIT,
  STATE_READ_CACHE_START,
  STATE_READ_CACHE_WAIT,
  STATE_WRITE_ENABLE,
  STATE_WRITE_CACHE_START,
  STATE_WRITE_CACHE,
  STATE_WRITE_PAGE_START,
  STATE_WRITE_PAGE_CHECK,
  STATE_WRITE_PAGE_WAIT,
  STATE_ERASE_ENABLE,
  STATE_ERASE_START,
  STATE_ERASE_CHECK,
  STATE_ERASE_WAIT,
  STATE_ERROR
};

struct [[gnu::packed]] DeviceId
{
  uint8_t manufacturer;
  uint8_t device;
};
/*----------------------------------------------------------------------------*/
static inline uint32_t addressToColumn(const struct MX35Serial *, uint32_t);
static inline uint32_t addressToRow(const struct MX35Serial *, uint32_t);
static void busAcquire(struct MX35Serial *);
static void busRelease(struct MX35Serial *);
static void cacheRead(struct MX35Serial *, uint32_t, void *, size_t);
static void cacheWrite(struct MX35Serial *, uint32_t, const void *, size_t);
static bool changeQuadMode(struct MX35Serial *, bool);
static void contextReset(struct MX35Serial *);
static bool disableBlockProtection(struct MX35Serial *);
static void eraseBlock(struct MX35Serial *, uint32_t);
static void interruptHandler(void *);
static void interruptHandlerTimer(void *);
static void pageProgram(struct MX35Serial *, uint32_t);
static void pageRead(struct MX35Serial *, uint32_t);
static void pollFeatureRegister(struct MX35Serial *, uint8_t);
static struct DeviceId readDeviceId(struct MX35Serial *);
static uint8_t readFeatureRegister(struct MX35Serial *, uint8_t);
static void waitMemoryBusy(struct MX35Serial *);
static void writeEnable(struct MX35Serial *);
static void writeFeatureRegister(struct MX35Serial *, uint8_t, uint8_t);
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *, const void *);
static void memoryDeinit(void *);
static void memorySetCallback(void *, void (*)(void *), void *);
static enum Result memoryGetParam(void *, int, void *);
static enum Result memorySetParam(void *, int, const void *);
static size_t memoryRead(void *, void *, size_t);
static size_t memoryWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const MX35Serial = &(const struct InterfaceClass){
    .size = sizeof(struct MX35Serial),
    .init = memoryInit,
    .deinit = memoryDeinit,

    .setCallback = memorySetCallback,
    .getParam = memoryGetParam,
    .setParam = memorySetParam,
    .read = memoryRead,
    .write = memoryWrite
};
/*----------------------------------------------------------------------------*/
static inline uint32_t addressToColumn(const struct MX35Serial *memory,
    uint32_t address)
{
  return address % memory->page;
}
/*----------------------------------------------------------------------------*/
static inline uint32_t addressToRow(const struct MX35Serial *memory,
    uint32_t address)
{
  return address / memory->page;
}
/*----------------------------------------------------------------------------*/
static void busAcquire(struct MX35Serial *memory)
{
  ifSetParam(memory->spi, IF_ACQUIRE, NULL);

  if (memory->rate)
    ifSetParam(memory->spi, IF_RATE, &memory->rate);

  ifSetParam(memory->spi, IF_SPI_MODE, &(uint8_t){0});
  ifSetParam(memory->spi, IF_SPI_UNIDIRECTIONAL, NULL);

  if (memory->blocking)
  {
    ifSetParam(memory->spi, IF_BLOCKING, NULL);
    ifSetCallback(memory->spi, NULL, NULL);
  }
  else
  {
    ifSetParam(memory->spi, IF_ZEROCOPY, NULL);
    ifSetCallback(memory->spi, interruptHandler, memory);
  }
}
/*----------------------------------------------------------------------------*/
static void busRelease(struct MX35Serial *memory)
{
  if (!memory->blocking)
    ifSetCallback(memory->spi, NULL, NULL);
  ifSetParam(memory->spi, IF_RELEASE, NULL);
}
/*----------------------------------------------------------------------------*/
static void cacheRead(struct MX35Serial *memory, uint32_t position,
    void *buffer, size_t length)
{
  const uint32_t column = addressToColumn(memory, position);

  memory->command[0] = CMD_READ_FROM_CACHE;
  memory->command[1] = column >> 8;
  memory->command[2] = column;
  memory->command[3] = 0xFF;

  if (memory->blocking)
  {
    pinReset(memory->cs);
    ifWrite(memory->spi, memory->command, 4);
    ifRead(memory->spi, buffer, length);
    pinSet(memory->cs);
  }
  else
  {
    pinReset(memory->cs);
    ifWrite(memory->spi, memory->command, 4);
  }
}
/*----------------------------------------------------------------------------*/
static void cacheWrite(struct MX35Serial *memory, uint32_t position,
    const void *buffer, size_t length)
{
  const uint32_t column = addressToColumn(memory, position);

  memory->command[0] = CMD_PROGRAM_LOAD;
  memory->command[1] = column >> 8;
  memory->command[2] = column;

  if (memory->blocking)
  {
    pinReset(memory->cs);
    ifWrite(memory->spi, memory->command, 3);
    ifWrite(memory->spi, buffer, length);
    pinSet(memory->cs);
  }
  else
  {
    pinReset(memory->cs);
    ifWrite(memory->spi, memory->command, 3);
  }
}
/*----------------------------------------------------------------------------*/
static bool changeErrorCorrectionMode(struct MX35Serial *memory, bool enabled)
{
  uint8_t current = readFeatureRegister(memory, FEATURE_CFG);
  const uint8_t expected = enabled ?
      (current | FR_CFG_ECC_ENABLE) : (current & ~FR_CFG_ECC_ENABLE);

  if (current != expected)
  {
    writeFeatureRegister(memory, FEATURE_CFG, expected);
    current = readFeatureRegister(memory, FEATURE_CFG);
  }

  return current == expected;
}
/*----------------------------------------------------------------------------*/
static bool changeQuadMode(struct MX35Serial *memory, bool enabled)
{
  uint8_t current = readFeatureRegister(memory, FEATURE_CFG);
  const uint8_t expected = enabled ?
      (current | FR_CFG_QE) : (current & ~FR_CFG_QE);

  if (current != expected)
  {
    writeFeatureRegister(memory, FEATURE_CFG, expected);
    current = readFeatureRegister(memory, FEATURE_CFG);
  }

  return current == expected;
}
/*----------------------------------------------------------------------------*/
static void contextReset(struct MX35Serial *memory)
{
  memory->context.buffer = 0;
  memory->context.left = 0;
  memory->context.length = 0;
  memory->context.position = 0;
  memory->context.state = STATE_IDLE;
}
/*----------------------------------------------------------------------------*/
static bool disableBlockProtection(struct MX35Serial *memory)
{
  uint8_t value = readFeatureRegister(memory, FEATURE_BP);

  if (value)
  {
    writeFeatureRegister(memory, FEATURE_BP, 0);
    value = readFeatureRegister(memory, FEATURE_BP);
  }

  return !value;
}
/*----------------------------------------------------------------------------*/
static void eraseBlock(struct MX35Serial *memory, uint32_t position)
{
  const uint32_t row = addressToRow(memory, position);

  memory->command[0] = CMD_BLOCK_ERASE;
  memory->command[1] = row >> 16;
  memory->command[2] = row >> 8;
  memory->command[3] = row;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 4);

  if (memory->blocking)
    pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *argument)
{
  struct MX35Serial * const memory = argument;
  const enum Result status = ifGetParam(memory->spi, IF_STATUS, NULL);
  bool event = false;

  assert(memory->context.state != STATE_IDLE
      && memory->context.state != STATE_ERROR);

  if (status != E_OK)
  {
    memory->context.state = STATE_ERROR;
    event = true;

    memory->context.buffer = 0;
    memory->context.length = 0;
    memory->context.position = 0;

    pinSet(memory->cs);
    busRelease(memory);
  }

  switch (memory->context.state)
  {
    case STATE_READ_PAGE_START:
      /* Release chip select */
      pinSet(memory->cs);

      /* Update context */
      memory->context.state = STATE_READ_PAGE_CHECK;

      /* Poll OIP bit in Status Feature Register */
      pollFeatureRegister(memory, FEATURE_STATUS);
      break;

    case STATE_READ_PAGE_CHECK:
      /* Update context */
      memory->context.state = STATE_READ_PAGE_WAIT;

      timerSetValue(memory->timer, 0);
      timerEnable(memory->timer);
      break;

    case STATE_READ_PAGE_WAIT:
      if (memory->command[0] & FR_STATUS_OIP)
      {
        /* Memory is still busy, restart the periodic timer */
        timerSetValue(memory->timer, 0);
        timerEnable(memory->timer);
      }
      else
      {
        uint8_t * const data = (uint8_t *)memory->context.buffer;
        const uint32_t position = memory->context.position;
        const uint32_t available = memory->page - position % memory->page;
        const uint32_t chunk = MIN(available, memory->context.left);

        /* Release chip select */
        pinSet(memory->cs);

        /* Update context */
        memory->context.state = STATE_READ_CACHE_START;
        memory->context.length = chunk;

        cacheRead(memory, position, data, chunk);
      }
      break;

    case STATE_READ_CACHE_START:
      /* Update context */
      memory->context.state = STATE_READ_CACHE_WAIT;

      ifRead(memory->spi, (uint8_t *)memory->context.buffer,
          memory->context.length);
      break;

    case STATE_READ_CACHE_WAIT:
      /* Release chip select */
      pinSet(memory->cs);

      /* Update context */
      memory->context.buffer += memory->context.length;
      memory->context.left -= memory->context.length;
      memory->context.position += memory->context.length;

      if (memory->context.left)
      {
        memory->context.state = STATE_READ_PAGE_START;
        pageRead(memory, memory->context.position);
      }
      else
      {
        memory->context.state = STATE_IDLE;
        event = true;

        memory->position = memory->context.position;
        if (memory->position == memory->capacity)
          memory->position = 0;

        busRelease(memory);
      }
      break;

    case STATE_WRITE_ENABLE:
    {
      const uint8_t * const data = (const uint8_t *)memory->context.buffer;
      const uint32_t position = memory->context.position;
      const uint32_t available = memory->page - position % memory->page;
      const uint32_t chunk = MIN(available, memory->context.left);

      /* Release chip select */
      pinSet(memory->cs);

      /* Update context */
      memory->context.length = chunk;
      memory->context.state = STATE_WRITE_CACHE_START;

      cacheWrite(memory, position, data, chunk);
      break;
    }

    case STATE_WRITE_CACHE_START:
      /* Update context */
      memory->context.state = STATE_WRITE_CACHE;

      ifWrite(memory->spi, (const uint8_t *)memory->context.buffer,
          memory->context.length);
      break;

    case STATE_WRITE_CACHE:
      /* Release chip select */
      pinSet(memory->cs);

      /* Update context */
      memory->context.state = STATE_WRITE_PAGE_START;

      pageProgram(memory, memory->context.position);
      break;

    case STATE_WRITE_PAGE_START:
      /* Release chip select */
      pinSet(memory->cs);

      /* Update context */
      memory->context.state = STATE_WRITE_PAGE_CHECK;

      /* Poll OIP bit in Status Feature Register */
      pollFeatureRegister(memory, FEATURE_STATUS);
      break;

    case STATE_WRITE_PAGE_CHECK:
      /* Update context */
      memory->context.state = STATE_WRITE_PAGE_WAIT;

      timerSetValue(memory->timer, 0);
      timerEnable(memory->timer);
      break;

    case STATE_WRITE_PAGE_WAIT:
      if (memory->command[0] & FR_STATUS_OIP)
      {
        /* Memory is still busy, restart the periodic timer */
        timerSetValue(memory->timer, 0);
        timerEnable(memory->timer);
      }
      else
      {
        /* Release chip select */
        pinSet(memory->cs);

        /* Update context */
        memory->context.buffer += memory->context.length;
        memory->context.left -= memory->context.length;
        memory->context.position += memory->context.length;

        if (memory->context.left)
        {
          memory->context.state = STATE_WRITE_ENABLE;
          writeEnable(memory);
        }
        else
        {
          memory->context.state = STATE_IDLE;
          event = true;

          memory->position = memory->context.position;
          if (memory->position == memory->capacity)
            memory->position = 0;

          busRelease(memory);
        }
      }
      break;

    case STATE_ERASE_ENABLE:
      /* Release chip select */
      pinSet(memory->cs);

      memory->context.state = STATE_ERASE_START;
      eraseBlock(memory, memory->context.position);
      break;

    case STATE_ERASE_START:
      /* Release chip select */
      pinSet(memory->cs);

      /* Poll OIP bit in Status Feature Register */
      memory->context.state = STATE_ERASE_CHECK;
      pollFeatureRegister(memory, FEATURE_STATUS);
      break;

    case STATE_ERASE_CHECK:
      memory->context.state = STATE_ERASE_WAIT;

      timerSetValue(memory->timer, 0);
      timerEnable(memory->timer);
      break;

    case STATE_ERASE_WAIT:
      if (memory->command[0] & FR_STATUS_OIP)
      {
        /* Memory is still busy, restart the periodic timer */
        timerSetValue(memory->timer, 0);
        timerEnable(memory->timer);
      }
      else
      {
        /* Release chip select */
        pinSet(memory->cs);

        memory->context.state = STATE_IDLE;
        event = true;

        busRelease(memory);
      }
      break;

    default:
      break;
  }

  if (event && memory->callback != NULL)
    memory->callback(memory->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void interruptHandlerTimer(void *argument)
{
  struct MX35Serial * const memory = argument;
  ifRead(memory->spi, memory->command, 1);
}
/*----------------------------------------------------------------------------*/
static void pageProgram(struct MX35Serial *memory, uint32_t position)
{
  const uint32_t row = addressToRow(memory, position);

  memory->command[0] = CMD_PROGRAM_EXECUTE;
  memory->command[1] = row >> 16;
  memory->command[2] = row >> 8;
  memory->command[3] = row;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 4);

  if (memory->blocking)
    pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static void pageRead(struct MX35Serial *memory, uint32_t position)
{
  const uint32_t row = addressToRow(memory, position);

  memory->command[0] = CMD_PAGE_READ;
  memory->command[1] = row >> 16;
  memory->command[2] = row >> 8;
  memory->command[3] = row;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 4);

  if (memory->blocking)
    pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static void pollFeatureRegister(struct MX35Serial *memory, uint8_t feature)
{
  memory->command[0] = CMD_GET_FEATURE;
  memory->command[1] = feature;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 1);
}
/*----------------------------------------------------------------------------*/
static struct DeviceId readDeviceId(struct MX35Serial *memory)
{
  memory->command[0] = CMD_READ_ID;
  memory->command[1] = 0;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 2);
  ifRead(memory->spi, memory->command, sizeof(struct DeviceId));
  pinSet(memory->cs);

  struct DeviceId id;
  memcpy(&id, memory->command, sizeof(struct DeviceId));

  return id;
}
/*----------------------------------------------------------------------------*/
static uint8_t readFeatureRegister(struct MX35Serial *memory, uint8_t feature)
{
  memory->command[0] = CMD_GET_FEATURE;
  memory->command[1] = feature;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 2);
  ifRead(memory->spi, memory->command, 1);
  pinSet(memory->cs);

  return memory->command[0];
}
/*----------------------------------------------------------------------------*/
static void waitMemoryBusy(struct MX35Serial *memory)
{
  uint8_t status;

  do
  {
    status = readFeatureRegister(memory, FEATURE_STATUS);
  }
  while (status & FR_STATUS_OIP);
}
/*----------------------------------------------------------------------------*/
static void writeEnable(struct MX35Serial *memory)
{
  memory->command[0] = CMD_WRITE_ENABLE;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 1);

  if (memory->blocking)
    pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static void writeFeatureRegister(struct MX35Serial *memory, uint8_t feature,
    uint8_t value)
{
  /* Enable write mode */
  writeEnable(memory);

  memory->command[0] = CMD_SET_FEATURE;
  memory->command[1] = feature;
  memory->command[2] = value;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 3);
  pinSet(memory->cs);

  /* Wait until write operation is completed */
  waitMemoryBusy(memory);
}
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *object, const void *configBase)
{
  const struct MX35SerialConfig * const config = configBase;
  assert(config != NULL);
  assert(config->spi != NULL);

  struct MX35Serial * const memory = object;
  enum Result res = E_OK;

  memory->cs = pinInit(config->cs);
  if (!pinValid(memory->cs))
    return E_VALUE;
  pinOutput(memory->cs, true);

  memory->callback = NULL;
  memory->spi = config->spi;
  memory->timer = config->timer;
  memory->position = 0;
  memory->blocking = true;
  memory->ecc = config->ecc;
  contextReset(memory);

  if (!config->rate)
  {
    if ((res = ifGetParam(memory->spi, IF_RATE, &memory->rate)) != E_OK)
      return res;
  }
  else
    memory->rate = config->rate;

  if (memory->timer != NULL)
  {
    /* Configure polling timer */
    const uint32_t frequency = !config->poll ? DEFAULT_POLL_RATE : config->poll;
    const uint32_t overflow = (timerGetFrequency(memory->timer) + frequency - 1)
        / frequency;

    if (!overflow)
      return E_VALUE;

    timerSetAutostop(memory->timer, true);
    timerSetCallback(memory->timer, interruptHandlerTimer, memory);
    timerSetOverflow(memory->timer, overflow);
  }

  /* Lock the interface */
  busAcquire(memory);
  /* Read device information */
  const struct DeviceId id = readDeviceId(memory);
  /* Unlock the interface */
  busRelease(memory);

  const struct MX35Info info = mx35GetDeviceInfo(id.manufacturer, id.device);

  if (!info.blocks)
    return E_DEVICE;
  if (memory->ecc && !info.ecc)
    return E_INTERFACE;

  if (config->spare)
    memory->page = memory->ecc ? MEMORY_PAGE_2K_SIZE : MEMORY_PAGE_2K_ECC_SIZE;
  else
    memory->page = 1UL << (MEMORY_PAGE_2K_COLUMN_SIZE - 1);

  if (info.wide)
    memory->page <<= 1;

  memory->capacity = memory->page * info.blocks * MEMORY_PAGES_PER_BLOCK;

  busAcquire(memory);
  /* Configure memory bus mode */
  if (!changeQuadMode(memory, false))
    res = E_INTERFACE;
  /* Configure ECC feature */
  if (!changeErrorCorrectionMode(memory, memory->ecc))
    res = E_INTERFACE;
  /* Disable all block protection features */
  if (!disableBlockProtection(memory))
    res = E_INTERFACE;
  busRelease(memory);

  return res;
}
/*----------------------------------------------------------------------------*/
static void memoryDeinit(void *object)
{
  struct MX35Serial * const memory = object;

  if (memory->timer != NULL)
    timerSetCallback(memory->timer, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
static void memorySetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct MX35Serial * const memory = object;

  memory->callbackArgument = argument;
  memory->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result memoryGetParam(void *object, int parameter, void *data)
{
  struct MX35Serial * const memory = object;

  switch ((enum FlashParameter)parameter)
  {
    case IF_FLASH_BLOCK_SIZE:
      *(uint32_t *)data = memory->page * MEMORY_PAGES_PER_BLOCK;
      return E_OK;

    case IF_FLASH_PAGE_SIZE:
      *(uint32_t *)data = memory->page;
      return E_OK;

    default:
      break;
  }

  switch ((enum IfParameter)parameter)
  {
    case IF_RATE:
      *(uint32_t *)data = memory->rate;
      return E_OK;

    case IF_POSITION:
      *(uint32_t *)data = memory->position;
      return E_OK;

    case IF_POSITION_64:
      *(uint64_t *)data = (uint64_t)memory->position;
      return E_OK;

    case IF_SIZE:
      *(uint32_t *)data = memory->capacity;
      return E_OK;

    case IF_SIZE_64:
      *(uint64_t *)data = (uint64_t)memory->capacity;
      return E_OK;

    case IF_STATUS:
      if (!memory->blocking)
      {
        if (memory->context.state == STATE_ERROR)
          return E_INTERFACE;
        else if (memory->context.state != STATE_IDLE)
          return E_BUSY;
        else
          return E_OK;
      }
      else
        return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result memorySetParam(void *object, int parameter, const void *data)
{
  struct MX35Serial * const memory = object;

  switch ((enum FlashParameter)parameter)
  {
    case IF_FLASH_ERASE_BLOCK:
    {
      const uint32_t position = *(const uint32_t *)data;

      if (position < memory->capacity)
      {
        if (memory->blocking)
        {
          contextReset(memory);

          busAcquire(memory);
          writeEnable(memory);
          eraseBlock(memory, position);
          waitMemoryBusy(memory);
          busRelease(memory);

          return E_OK;
        }
        else
        {
          /* Unused fields */
          memory->context.buffer = 0;
          memory->context.left = 0;
          memory->context.length = 0;
          /* Setup context */
          memory->context.position = position;
          memory->context.state = STATE_ERASE_ENABLE;

          busAcquire(memory);
          writeEnable(memory);

          return E_BUSY;
        }
      }
      else
        return E_ADDRESS;
    }

    default:
      break;
  }

  switch ((enum IfParameter)parameter)
  {
    case IF_RATE:
    {
      const enum Result res = ifSetParam(memory->spi, IF_RATE, data);

      if (res == E_OK)
        memory->rate = *(const uint32_t *)data;
      return res;
    }

    case IF_POSITION:
    {
      const uint32_t position = *(const uint32_t *)data;

      if (position < memory->capacity)
      {
        memory->position = position;
        return E_OK;
      }
      else
        return E_ADDRESS;
    }

    case IF_POSITION_64:
    {
      const uint64_t position = *(const uint64_t *)data;

      if (position < (uint64_t)memory->capacity)
      {
        memory->position = (uint32_t)position;
        return E_OK;
      }
      else
        return E_ADDRESS;
    }

    case IF_BLOCKING:
      memory->blocking = true;
      return E_OK;

    case IF_ZEROCOPY:
      assert(memory->timer != NULL);
      memory->blocking = false;
      return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static size_t memoryRead(void *object, void *buffer, size_t length)
{
  struct MX35Serial * const memory = object;

  if (length > memory->capacity - memory->position)
    length = memory->capacity - memory->position;

  if (memory->blocking)
  {
    uint8_t *data = buffer;
    uint32_t left = (uint32_t)length;
    uint32_t position = memory->position;

    contextReset(memory);
    busAcquire(memory);

    while (left)
    {
      const uint32_t available = memory->page - position % memory->page;
      const uint32_t chunk = MIN(available, left);

      pageRead(memory, position);
      waitMemoryBusy(memory);
      cacheRead(memory, position, data, chunk);

      left -= chunk;
      data += chunk;
      position += chunk;
    }

    busRelease(memory);

    memory->position += length;
    if (memory->position == memory->capacity)
      memory->position = 0;
  }
  else
  {
    /* Setup context */
    memory->context.buffer = (uintptr_t)buffer;
    memory->context.left = length;
    memory->context.length = 0;
    memory->context.position = memory->position;
    memory->context.state = STATE_READ_PAGE_START;

    busAcquire(memory);
    pageRead(memory, memory->position);
  }

  return length;
}
/*----------------------------------------------------------------------------*/
static size_t memoryWrite(void *object, const void *buffer, size_t length)
{
  struct MX35Serial * const memory = object;

  if (length > memory->capacity - memory->position)
    length = memory->capacity - memory->position;

  if (memory->blocking)
  {
    const uint8_t *data = buffer;
    uint32_t left = (uint32_t)length;
    uint32_t position = memory->position;

    contextReset(memory);
    busAcquire(memory);

    while (left)
    {
      const uint32_t available = memory->page - position % memory->page;
      const uint32_t chunk = MIN(available, left);

      writeEnable(memory);
      cacheWrite(memory, position, data, chunk);
      pageProgram(memory, position);
      waitMemoryBusy(memory);

      left -= chunk;
      data += chunk;
      position += chunk;
    }

    busRelease(memory);

    memory->position += length;
    if (memory->position == memory->capacity)
      memory->position = 0;
  }
  else
  {
    /* Setup context */
    memory->context.buffer = (uintptr_t)buffer;
    memory->context.left = length;
    memory->context.length = 0;
    memory->context.position = memory->position;
    memory->context.state = STATE_WRITE_ENABLE;

    busAcquire(memory);
    writeEnable(memory);
  }

  return length;
}
