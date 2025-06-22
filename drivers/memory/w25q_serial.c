/*
 * w25q_serial.c
 * Copyright (C) 2024 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/memory/nor_defs.h>
#include <dpm/memory/w25q_defs.h>
#include <dpm/memory/w25q_serial.h>
#include <halm/delay.h>
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
  STATE_READ_START,
  STATE_READ_WAIT,
  STATE_WRITE_ENABLE,
  STATE_WRITE_SETUP,
  STATE_WRITE_START,
  STATE_WRITE_CHECK,
  STATE_WRITE_WAIT,
  STATE_ERASE_ENABLE,
  STATE_ERASE_START,
  STATE_ERASE_CHECK,
  STATE_ERASE_WAIT,
  STATE_ERROR
};
/*----------------------------------------------------------------------------*/
static void busAcquire(struct W25QSerial *);
static void busRelease(struct W25QSerial *);
static bool changeDriverStrength(struct W25QSerial *, enum W25DriverStrength);
static void changePowerDownMode(struct W25QSerial *, bool);
static bool changeQuadMode(struct W25QSerial *, bool);
static void contextReset(struct W25QSerial *);
static void eraseBlock64KB(struct W25QSerial *, uint32_t);
static void eraseSector4KB(struct W25QSerial *, uint32_t);
static void exitQpiXipMode(struct W25QSerial *);
static uint32_t getCapacityFromInfo(uint8_t);
static void interruptHandler(void *);
static void interruptHandlerTimer(void *);
static void pageProgram(struct W25QSerial *, uint32_t, const void *, size_t);
static void pageRead(struct W25QSerial *, uint32_t, void *, size_t);
static void pollStatusRegister(struct W25QSerial *, uint8_t);
static struct JedecInfo readJedecInfo(struct W25QSerial *);
static uint8_t readStatusRegister(struct W25QSerial *, uint8_t);
static void waitMemoryBusy(struct W25QSerial *);
static void writeEnable(struct W25QSerial *, bool);
static void writeStatusRegister(struct W25QSerial *, uint8_t, uint8_t, bool);
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *, const void *);
static void memoryDeinit(void *);
static void memorySetCallback(void *, void (*)(void *), void *);
static enum Result memoryGetParam(void *, int, void *);
static enum Result memorySetParam(void *, int, const void *);
static size_t memoryRead(void *, void *, size_t);
static size_t memoryWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const W25QSerial = &(const struct InterfaceClass){
    .size = sizeof(struct W25QSerial),
    .init = memoryInit,
    .deinit = memoryDeinit,

    .setCallback = memorySetCallback,
    .getParam = memoryGetParam,
    .setParam = memorySetParam,
    .read = memoryRead,
    .write = memoryWrite
};
/*----------------------------------------------------------------------------*/
static void busAcquire(struct W25QSerial *memory)
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
static void busRelease(struct W25QSerial *memory)
{
  if (!memory->blocking)
    ifSetCallback(memory->spi, NULL, NULL);
  ifSetParam(memory->spi, IF_RELEASE, NULL);
}
/*----------------------------------------------------------------------------*/
static bool changeDriverStrength(struct W25QSerial *memory,
    enum W25DriverStrength strength)
{
  if (strength == W25_DRV_DEFAULT)
    return true;

  uint8_t current = readStatusRegister(memory, CMD_READ_STATUS_REGISTER_3);
  const uint8_t expected = (current & ~SR3_DRV_MASK) | SR3_DRV(strength - 1);

  if (current != expected)
  {
    writeStatusRegister(memory, CMD_WRITE_STATUS_REGISTER_3, expected, false);
    current = readStatusRegister(memory, CMD_READ_STATUS_REGISTER_3);
  }

  return current == expected;
}
/*----------------------------------------------------------------------------*/
static void changePowerDownMode(struct W25QSerial *memory, bool enable)
{
  memory->command[0] = enable ? CMD_POWER_DOWN : CMD_POWER_DOWN_RELEASE;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 1);
  pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static bool changeQuadMode(struct W25QSerial *memory, bool enabled)
{
  uint8_t current = readStatusRegister(memory, CMD_READ_STATUS_REGISTER_2);
  const uint8_t expected = enabled ? (current | SR2_QE) : (current & ~SR2_QE);

  if (current != expected)
  {
    writeStatusRegister(memory, CMD_WRITE_STATUS_REGISTER_2, expected, false);
    current = readStatusRegister(memory, CMD_READ_STATUS_REGISTER_2);
  }

  return current == expected;
}
/*----------------------------------------------------------------------------*/
static void contextReset(struct W25QSerial *memory)
{
  memory->context.buffer = 0;
  memory->context.left = 0;
  memory->context.length = 0;
  memory->context.position = 0;
  memory->context.state = STATE_IDLE;
}
/*----------------------------------------------------------------------------*/
static void eraseBlock64KB(struct W25QSerial *memory, uint32_t position)
{
  size_t commandBufferLength;

  if (memory->extended)
  {
    commandBufferLength = 5;

    memory->command[0] = CMD_BLOCK_ERASE_64KB_4BYTE;
    memory->command[1] = position >> 24;
    memory->command[2] = position >> 16;
    memory->command[3] = position >> 8;
    memory->command[4] = position;
  }
  else
  {
    commandBufferLength = 4;

    memory->command[0] = CMD_BLOCK_ERASE_64KB;
    memory->command[1] = position >> 16;
    memory->command[2] = position >> 8;
    memory->command[3] = position;
  }

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, commandBufferLength);

  if (memory->blocking)
    pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static void eraseSector4KB(struct W25QSerial *memory, uint32_t position)
{
  size_t commandBufferLength;

  if (memory->extended)
  {
    commandBufferLength = 5;

    memory->command[0] = CMD_SECTOR_ERASE_4BYTE;
    memory->command[1] = position >> 24;
    memory->command[2] = position >> 16;
    memory->command[3] = position >> 8;
    memory->command[4] = position;
  }
  else
  {
    commandBufferLength = 4;

    memory->command[0] = CMD_SECTOR_ERASE;
    memory->command[1] = position >> 16;
    memory->command[2] = position >> 8;
    memory->command[3] = position;
  }

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, commandBufferLength);

  if (memory->blocking)
    pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static void exitQpiXipMode(struct W25QSerial *memory)
{
  uint8_t pattern[3]; // TODO
  memset(pattern, XIP_MODE_EXIT, sizeof(pattern));

  pinReset(memory->cs);
  ifWrite(memory->spi, pattern, sizeof(pattern));
  pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static uint32_t getCapacityFromInfo(uint8_t capacity)
{
  return (capacity >= 0x15 && capacity <= 0x22) ? (1UL << capacity) : 0;
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *argument)
{
  struct W25QSerial * const memory = argument;
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
    case STATE_READ_START:
      memory->context.state = STATE_READ_WAIT;

      ifRead(memory->spi, (uint8_t *)memory->context.buffer,
          memory->context.length);
      break;

    case STATE_READ_WAIT:
      /* Release chip select */
      pinSet(memory->cs);

      memory->context.state = STATE_IDLE;
      event = true;

      memory->position += memory->context.length;
      if (memory->position == memory->capacity)
        memory->position = 0;

      busRelease(memory);
      break;

    case STATE_WRITE_ENABLE:
    {
      const uint8_t * const data = (const uint8_t *)memory->context.buffer;
      const uint32_t position = memory->context.position;
      const uint32_t available = MEMORY_PAGE_SIZE
          - (position & (MEMORY_PAGE_SIZE - 1));
      const uint32_t chunk = MIN(available, memory->context.left);

      /* Release chip select */
      pinSet(memory->cs);

      /* Update context */
      memory->context.length = chunk;
      memory->context.state = STATE_WRITE_SETUP;

      pageProgram(memory, position, data, chunk);
      break;
    }

    case STATE_WRITE_SETUP:
      memory->context.state = STATE_WRITE_START;

      ifWrite(memory->spi, (const uint8_t *)memory->context.buffer,
          memory->context.length);

      /* Update context */
      memory->context.buffer += memory->context.length;
      memory->context.left -= memory->context.length;
      memory->context.position += memory->context.length;
      break;

    case STATE_WRITE_START:
      /* Release chip select */
      pinSet(memory->cs);

      /* Poll BUSY bit in SR1 */
      memory->context.state = STATE_WRITE_CHECK;
      pollStatusRegister(memory, CMD_READ_STATUS_REGISTER_1);
      break;

    case STATE_WRITE_CHECK:
      memory->context.state = STATE_WRITE_WAIT;

      timerSetValue(memory->timer, 0);
      timerEnable(memory->timer);
      break;

    case STATE_WRITE_WAIT:
      if (memory->command[0] & SR1_BUSY)
      {
        /* Memory is still busy, restart the periodic timer */
        timerSetValue(memory->timer, 0);
        timerEnable(memory->timer);
      }
      else
      {
        /* Release chip select */
        pinSet(memory->cs);

        if (memory->context.left)
        {
          memory->context.state = STATE_WRITE_ENABLE;
          writeEnable(memory, true);
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
      assert(memory->context.length == MEMORY_SECTOR_4KB_SIZE
          || memory->context.length == MEMORY_BLOCK_64KB_SIZE);

      /* Release chip select */
      pinSet(memory->cs);

      memory->context.state = STATE_ERASE_START;
      if (memory->context.length == MEMORY_SECTOR_4KB_SIZE)
        eraseSector4KB(memory, memory->context.position);
      else
        eraseBlock64KB(memory, memory->context.position);
      break;

    case STATE_ERASE_START:
      /* Release chip select */
      pinSet(memory->cs);

      /* Poll BUSY bit in SR1 */
      memory->context.state = STATE_ERASE_CHECK;
      pollStatusRegister(memory, CMD_READ_STATUS_REGISTER_1);
      break;

    case STATE_ERASE_CHECK:
      memory->context.state = STATE_ERASE_WAIT;

      timerSetValue(memory->timer, 0);
      timerEnable(memory->timer);
      break;

    case STATE_ERASE_WAIT:
      if (memory->command[0] & SR1_BUSY)
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
  struct W25QSerial * const memory = argument;
  ifRead(memory->spi, memory->command, 1);
}
/*----------------------------------------------------------------------------*/
static void pageProgram(struct W25QSerial *memory, uint32_t position,
    const void *buffer, size_t length)
{
  size_t commandBufferLength;

  if (memory->extended)
  {
    commandBufferLength = 5;

    memory->command[0] = CMD_PAGE_PROGRAM_4BYTE;
    memory->command[1] = position >> 24;
    memory->command[2] = position >> 16;
    memory->command[3] = position >> 8;
    memory->command[4] = position;
  }
  else
  {
    commandBufferLength = 4;

    memory->command[0] = CMD_PAGE_PROGRAM;
    memory->command[1] = position >> 16;
    memory->command[2] = position >> 8;
    memory->command[3] = position;
  }

  if (memory->blocking)
  {
    pinReset(memory->cs);
    ifWrite(memory->spi, memory->command, commandBufferLength);
    ifWrite(memory->spi, buffer, length);
    pinSet(memory->cs);
  }
  else
  {
    pinReset(memory->cs);
    ifWrite(memory->spi, memory->command, commandBufferLength);
  }
}
/*----------------------------------------------------------------------------*/
static void pageRead(struct W25QSerial *memory, uint32_t position,
    void *buffer, size_t length)
{
  size_t commandBufferLength;

  if (memory->extended)
  {
    commandBufferLength = 6;

    memory->command[0] = CMD_FAST_READ_4BYTE;
    memory->command[1] = position >> 24;
    memory->command[2] = position >> 16;
    memory->command[3] = position >> 8;
    memory->command[4] = position;
    memory->command[5] = 0xFF;
  }
  else
  {
    commandBufferLength = 5;

    memory->command[0] = CMD_FAST_READ;
    memory->command[1] = position >> 16;
    memory->command[2] = position >> 8;
    memory->command[3] = position;
    memory->command[4] = 0xFF;
  }

  if (memory->blocking)
  {
    pinReset(memory->cs);
    ifWrite(memory->spi, memory->command, commandBufferLength);
    ifRead(memory->spi, buffer, length);
    pinSet(memory->cs);
  }
  else
  {
    pinReset(memory->cs);
    ifWrite(memory->spi, memory->command, commandBufferLength);
  }
}
/*----------------------------------------------------------------------------*/
static void pollStatusRegister(struct W25QSerial *memory, uint8_t command)
{
  memory->command[0] = command;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 1);
}
/*----------------------------------------------------------------------------*/
static struct JedecInfo readJedecInfo(struct W25QSerial *memory)
{
  memory->command[0] = CMD_READ_JEDEC_ID;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 1);
  ifRead(memory->spi, memory->command, sizeof(struct JedecInfo));
  pinSet(memory->cs);

  struct JedecInfo info;
  memcpy(&info, memory->command, sizeof(struct JedecInfo));

  return info;
}
/*----------------------------------------------------------------------------*/
static uint8_t readStatusRegister(struct W25QSerial *memory, uint8_t command)
{
  memory->command[0] = command;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 1);
  ifRead(memory->spi, memory->command, 1);
  pinSet(memory->cs);

  return memory->command[0];
}
/*----------------------------------------------------------------------------*/
static void waitMemoryBusy(struct W25QSerial *memory)
{
  uint8_t status;

  do
  {
    status = readStatusRegister(memory, CMD_READ_STATUS_REGISTER_1);
  }
  while (status & SR1_BUSY);
}
/*----------------------------------------------------------------------------*/
static void writeEnable(struct W25QSerial *memory, bool nonvolatile)
{
  memory->command[0] = nonvolatile ?
      CMD_WRITE_ENABLE : CMD_WRITE_ENABLE_VOLATILE;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 1);

  if (memory->blocking)
    pinSet(memory->cs);
}
/*----------------------------------------------------------------------------*/
static void writeStatusRegister(struct W25QSerial *memory, uint8_t command,
    uint8_t value, bool nonvolatile)
{
  /* Enable write mode */
  writeEnable(memory, nonvolatile);

  memory->command[0] = command;
  memory->command[1] = value;

  pinReset(memory->cs);
  ifWrite(memory->spi, memory->command, 2);
  pinSet(memory->cs);

  /* Wait until write operation is completed */
  waitMemoryBusy(memory);
}
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *object, const void *configBase)
{
  const struct W25QSerialConfig * const config = configBase;
  assert(config != NULL);
  assert(config->spi != NULL);
  assert(config->strength < W25_DRV_END);

  struct W25QSerial * const memory = object;
  struct JedecInfo info;
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
  memory->extended = false;
  memory->subsectors = false;
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
  /* Exit power down mode */
  changePowerDownMode(memory, false);
  /* Unlock the interface */
  busRelease(memory);

  udelay(MEMORY_RESET_TIMEOUT);

  /* Lock the interface */
  busAcquire(memory);
  /* Reset interface mode on the memory side */
  exitQpiXipMode(memory);
  /* Read device information */
  info = readJedecInfo(memory);
  /* Unlock the interface */
  busRelease(memory);

  const uint16_t capabilities = norGetCapabilitiesByJedecInfo(&info);

  if (!capabilities)
    return E_DEVICE;
  if (!(capabilities & NOR_HAS_SPI))
    return E_INTERFACE;

  memory->capacity = getCapacityFromInfo(info.capacity);
  if (!memory->capacity)
    return E_DEVICE;
  if (memory->capacity > (1UL << 24))
    memory->extended = true;

  if (capabilities & NOR_HAS_BLOCKS_4K)
    memory->subsectors = true;

  busAcquire(memory);
  /* Configure memory bus mode */
  if (!changeQuadMode(memory, false))
    res = E_INTERFACE;
  /* Configure driver strength */
  if (!changeDriverStrength(memory, config->strength))
    res = E_INTERFACE;
  busRelease(memory);

  return res;
}
/*----------------------------------------------------------------------------*/
static void memoryDeinit(void *object)
{
  struct W25QSerial * const memory = object;

  if (memory->timer != NULL)
  {
    timerDisable(memory->timer);
    timerSetCallback(memory->timer, NULL, NULL);
  }
}
/*----------------------------------------------------------------------------*/
static void memorySetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct W25QSerial * const memory = object;

  memory->callbackArgument = argument;
  memory->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result memoryGetParam(void *object, int parameter, void *data)
{
  struct W25QSerial * const memory = object;

  switch ((enum FlashParameter)parameter)
  {
    case IF_FLASH_BLOCK_SIZE:
      *(uint32_t *)data = MEMORY_BLOCK_64KB_SIZE;
      return E_OK;

    case IF_FLASH_SECTOR_SIZE:
      if (memory->subsectors)
      {
        *(uint32_t *)data = MEMORY_SECTOR_4KB_SIZE;
        return E_OK;
      }
      else
        return E_DEVICE;

    case IF_FLASH_PAGE_SIZE:
      *(uint32_t *)data = MEMORY_PAGE_SIZE;
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
  struct W25QSerial * const memory = object;

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
          writeEnable(memory, true);
          eraseBlock64KB(memory, position);
          waitMemoryBusy(memory);
          busRelease(memory);

          return E_OK;
        }
        else
        {
          /* Unused fields */
          memory->context.buffer = 0;
          memory->context.left = 0;
          /* Setup context */
          memory->context.length = MEMORY_BLOCK_64KB_SIZE;
          memory->context.position = position;
          memory->context.state = STATE_ERASE_ENABLE;

          busAcquire(memory);
          writeEnable(memory, true);

          return E_BUSY;
        }
      }
      else
        return E_ADDRESS;
    }

    case IF_FLASH_ERASE_SECTOR:
    {
      const uint32_t position = *(const uint32_t *)data;

      if (!memory->subsectors)
        return E_DEVICE;

      if (position < memory->capacity)
      {
        if (memory->blocking)
        {
          contextReset(memory);

          busAcquire(memory);
          writeEnable(memory, true);
          eraseSector4KB(memory, position);
          waitMemoryBusy(memory);
          busRelease(memory);

          return E_OK;
        }
        else
        {
          /* Unused fields */
          memory->context.buffer = 0;
          memory->context.left = 0;
          /* Setup context */
          memory->context.length = MEMORY_SECTOR_4KB_SIZE;
          memory->context.position = position;
          memory->context.state = STATE_ERASE_ENABLE;

          busAcquire(memory);
          writeEnable(memory, true);

          return E_BUSY;
        }
      }
      else
        return E_ADDRESS;
    }

    case IF_FLASH_SUSPEND:
      changePowerDownMode(memory, true);
      return E_OK;

    case IF_FLASH_RESUME:
      changePowerDownMode(memory, false);
      return E_OK;

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
  struct W25QSerial * const memory = object;

  if (length > memory->capacity - memory->position)
    length = memory->capacity - memory->position;

  if (memory->blocking)
  {
    contextReset(memory);

    busAcquire(memory);
    pageRead(memory, memory->position, buffer, length);
    busRelease(memory);

    memory->position += length;
    if (memory->position == memory->capacity)
      memory->position = 0;
  }
  else
  {
    /* Unused fields */
    memory->context.left = 0;
    memory->context.position = 0;
    /* Setup context */
    memory->context.buffer = (uintptr_t)buffer;
    memory->context.length = length;
    memory->context.state = STATE_READ_START;

    busAcquire(memory);
    pageRead(memory, memory->position, buffer, length);
  }

  return length;
}
/*----------------------------------------------------------------------------*/
static size_t memoryWrite(void *object, const void *buffer, size_t length)
{
  struct W25QSerial * const memory = object;

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
      const uint32_t available = MEMORY_PAGE_SIZE
          - (position & (MEMORY_PAGE_SIZE - 1));
      const uint32_t chunk = MIN(available, left);

      writeEnable(memory, true);
      pageProgram(memory, position, data, chunk);
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
    writeEnable(memory, true);
  }

  return length;
}
