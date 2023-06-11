/*
 * w25_spim.c
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/memory/w25_defs.h>
#include <dpm/memory/w25_spim.h>
#include <halm/generic/flash.h>
#include <halm/generic/spim.h>
#include <xcore/memory.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
enum
{
  STATE_IDLE,
  STATE_READ_WAIT,
  STATE_WRITE_ENABLE,
  STATE_WRITE_START,
  STATE_WRITE_WAIT,
  STATE_ERASE_ENABLE,
  STATE_ERASE_START,
  STATE_ERASE_WAIT,
  STATE_ERROR
};
/*----------------------------------------------------------------------------*/
static void busAcquire(struct W25SPIM *);
static void busRelease(struct W25SPIM *);
static bool changeQuadMode(struct W25SPIM *, bool);
static void enableQpiMode(struct W25SPIM *);
static void eraseBlock64KB(struct W25SPIM *, uint32_t);
static void eraseSector4KB(struct W25SPIM *, uint32_t);
static void exitQpiMode(struct W25SPIM *);
static void interruptHandler(void *);
static void pageProgram(struct W25SPIM *, uint32_t, const void *, size_t);
static void pageRead(struct W25SPIM *, uint32_t, void *, size_t);
static void pollStatusRegister(struct W25SPIM *, uint8_t, uint8_t);
static struct JedecInfo readJedecInfo(struct W25SPIM *);
static uint8_t readStatusRegister(struct W25SPIM *, uint8_t);
static void waitMemoryBusy(struct W25SPIM *);
static void writeEnable(struct W25SPIM *, bool);
static void writeStatusRegister(struct W25SPIM *, uint8_t, uint8_t, bool);
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *, const void *);
static void memoryDeinit(void *);
static void memorySetCallback(void *, void (*)(void *), void *);
static enum Result memoryGetParam(void *, int, void *);
static enum Result memorySetParam(void *, int, const void *);
static size_t memoryRead(void *, void *, size_t);
static size_t memoryWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const W25SPIM = &(const struct InterfaceClass){
    .size = sizeof(struct W25SPIM),
    .init = memoryInit,
    .deinit = memoryDeinit,

    .setCallback = memorySetCallback,
    .getParam = memoryGetParam,
    .setParam = memorySetParam,
    .read = memoryRead,
    .write = memoryWrite
};
/*----------------------------------------------------------------------------*/
static void busAcquire(struct W25SPIM *memory)
{
  ifSetParam(memory->spim, IF_ACQUIRE, 0);
  ifSetParam(memory->spim, memory->quad ? IF_SPIM_QUAD : IF_SPIM_DUAL, 0);

  if (!memory->blocking)
  {
    ifSetParam(memory->spim, IF_ZEROCOPY, 0);
    ifSetCallback(memory->spim, interruptHandler, memory);
  }
  else
    ifSetParam(memory->spim, IF_BLOCKING, 0);
}
/*----------------------------------------------------------------------------*/
static void busRelease(struct W25SPIM *memory)
{
  if (!memory->blocking)
    ifSetCallback(memory->spim, 0, 0);
  ifSetParam(memory->spim, IF_RELEASE, 0);
}
/*----------------------------------------------------------------------------*/
static bool changeQuadMode(struct W25SPIM *memory, bool enabled)
{
  const uint8_t mask = enabled ? SR2_QE : 0;
  uint8_t status = readStatusRegister(memory, CMD_READ_STATUS_REGISTER_2);

  if ((status & SR2_QE) != mask)
  {
    writeStatusRegister(memory, CMD_WRITE_STATUS_REGISTER_2, mask, false);
    status = readStatusRegister(memory, CMD_READ_STATUS_REGISTER_2);
  }

  return (status & SR2_QE) == mask;
}
/*----------------------------------------------------------------------------*/
static void enableQpiMode(struct W25SPIM *memory)
{
  const uint8_t command = CMD_ENTER_QPI;

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, 0);

  ifWrite(memory->spim, 0, 0);
}
/*----------------------------------------------------------------------------*/
static void eraseBlock64KB(struct W25SPIM *memory, uint32_t position)
{
  const uint32_t address = toLittleEndian32(position);
  uint8_t command;

  if (memory->extended)
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_32, &address);
    command = CMD_BLOCK_ERASE_64KB_4BYTE;
  }
  else
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &address);
    command = CMD_BLOCK_ERASE_64KB;
  }

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, 0);

  ifWrite(memory->spim, 0, 0);
}
/*----------------------------------------------------------------------------*/
static void eraseSector4KB(struct W25SPIM *memory, uint32_t position)
{
  const uint32_t address = toLittleEndian32(position);
  uint8_t command;

  if (memory->extended)
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_32, &address);
    command = CMD_SECTOR_ERASE_4BYTE;
  }
  else
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &address);
    command = CMD_SECTOR_ERASE;
  }

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, 0);

  ifWrite(memory->spim, 0, 0);
}
/*----------------------------------------------------------------------------*/
static void exitQpiMode(struct W25SPIM *memory)
{
  const uint8_t command = CMD_EXIT_QPI;

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_PARALLEL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, 0);

  ifWrite(memory->spim, 0, 0);
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *argument)
{
  struct W25SPIM * const memory = argument;
  const enum Result status = ifGetParam(memory->spim, IF_STATUS, 0);
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
    busRelease(memory);
  }

  switch (memory->context.state)
  {
    case STATE_READ_WAIT:
      memory->context.state = STATE_IDLE;
      event = true;

      memory->position += memory->context.length;
      if (memory->position >= memory->capacity)
        memory->position = 0;

      memory->context.length = 0;
      busRelease(memory);
      break;

    case STATE_WRITE_ENABLE:
    {
      const uint8_t * const data = (const uint8_t *)memory->context.buffer;
      const uint32_t position = memory->context.position;
      const uint32_t available = MEMORY_PAGE_SIZE
          - (position & (MEMORY_PAGE_SIZE - 1));
      const uint32_t chunk = MIN(available, memory->context.left);

      memory->context.buffer = data + chunk;
      memory->context.left -= chunk;
      memory->context.position += chunk;
      memory->context.state = STATE_WRITE_START;

      pageProgram(memory, position, data, chunk);
      break;
    }

    case STATE_WRITE_START:
      /* Poll BUSY bit in SR1 */
      memory->context.state = STATE_WRITE_WAIT;
      pollStatusRegister(memory, CMD_READ_STATUS_REGISTER_1, 0);
      break;

    case STATE_WRITE_WAIT:
      if (memory->context.left)
      {
        memory->context.state = STATE_WRITE_ENABLE;
        writeEnable(memory, true);
      }
      else
      {
        memory->context.state = STATE_IDLE;
        event = true;

        memory->position += memory->context.length;
        if (memory->position >= memory->capacity)
          memory->position = 0;

        memory->context.buffer = 0;
        memory->context.left = 0;
        memory->context.length = 0;
        memory->context.position = 0;
        busRelease(memory);
      }
      break;

    case STATE_ERASE_ENABLE:
      assert(memory->context.length == MEMORY_SECTOR_4KB_SIZE
          || memory->context.length == MEMORY_BLOCK_64KB_SIZE);

      memory->context.state = STATE_ERASE_START;
      if (memory->context.length == MEMORY_SECTOR_4KB_SIZE)
        eraseSector4KB(memory, memory->context.position);
      else
        eraseBlock64KB(memory, memory->context.position);
      break;

    case STATE_ERASE_START:
      /* Poll BUSY bit in SR1 */
      memory->context.state = STATE_ERASE_WAIT;
      pollStatusRegister(memory, CMD_READ_STATUS_REGISTER_1, 0);
      break;

    case STATE_ERASE_WAIT:
      memory->context.state = STATE_IDLE;
      event = true;

      memory->context.length = 0;
      memory->context.position = 0;
      busRelease(memory);
      break;

    default:
      break;
  }

  if (event && memory->callback)
    memory->callback(memory->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void pageProgram(struct W25SPIM *memory, uint32_t position,
    const void *buffer, size_t length)
{
  const uint32_t address = toLittleEndian32(position);
  const uint32_t count = toLittleEndian32(length);
  uint8_t command;

  if (memory->extended)
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_32, &address);
    command = memory->quad ?
        CMD_PAGE_PROGRAM_QUAD_INPUT_4BYTE : CMD_PAGE_PROGRAM_4BYTE;
  }
  else
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &address);
    command = memory->quad ?
        CMD_PAGE_PROGRAM_QUAD_INPUT : CMD_PAGE_PROGRAM;
  }

  if (memory->quad)
    ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, 0);
  else
    ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, 0);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &count);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);

  ifWrite(memory->spim, buffer, length);
}
/*----------------------------------------------------------------------------*/
static void pageRead(struct W25SPIM *memory, uint32_t position,
    void *buffer, size_t length)
{
  static const uint32_t post = 0xFF; /* Continuous read mode */
  static const uint8_t delay = 2; /* 4 clock cycles on QUAD IO */
  const uint32_t address = toLittleEndian32(position);
  const uint32_t count = toLittleEndian32(length);
  uint8_t command;

  if (memory->extended)
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_32, &address);
    command = memory->quad ?
        CMD_FAST_READ_QUAD_IO_4BYTE : CMD_FAST_READ_DUAL_IO_4BYTE;
  }
  else
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &address);
    command = memory->quad ? CMD_FAST_READ_QUAD_IO : CMD_FAST_READ_DUAL_IO;
  }

  if (memory->quad)
    ifSetParam(memory->spim, IF_SPIM_DELAY_LENGTH, &delay);
  else
    ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_8, &post);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &count);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_PARALLEL, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_PARALLEL, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_PARALLEL, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, 0);

  ifRead(memory->spim, buffer, length);
}
/*----------------------------------------------------------------------------*/
static void pollStatusRegister(struct W25SPIM *memory, uint8_t command,
    uint8_t bit)
{
  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_POLL_BIT, &bit);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, 0);

  ifRead(memory->spim, 0, 0);
}
/*----------------------------------------------------------------------------*/
static struct JedecInfo readJedecInfo(struct W25SPIM *memory)
{
  static const uint8_t command = CMD_READ_JEDEC_ID;
  static const uint32_t length = TO_LITTLE_ENDIAN_32(sizeof(struct JedecInfo));
  struct JedecInfo info;

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &length);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, 0);

  ifRead(memory->spim, &info, sizeof(info));
  return info;
}
/*----------------------------------------------------------------------------*/
static uint8_t readStatusRegister(struct W25SPIM *memory, uint8_t command)
{
  static const uint32_t length = TO_LITTLE_ENDIAN_32(1);
  uint8_t data;

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &length);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, 0);

  ifRead(memory->spim, &data, 1);
  return data;
}
/*----------------------------------------------------------------------------*/
static void waitMemoryBusy(struct W25SPIM *memory)
{
  uint8_t status;

  do
  {
    status = readStatusRegister(memory, CMD_READ_STATUS_REGISTER_1);
  }
  while (status & SR1_BUSY);
}
/*----------------------------------------------------------------------------*/
static void writeEnable(struct W25SPIM *memory, bool nonvolatile)
{
  const uint8_t command = nonvolatile ?
      CMD_WRITE_ENABLE : CMD_WRITE_ENABLE_VOLATILE;

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, 0);

  ifWrite(memory->spim, 0, 0);
}
/*----------------------------------------------------------------------------*/
static void writeStatusRegister(struct W25SPIM *memory, uint8_t command,
    uint8_t value, bool nonvolatile)
{
  static const uint32_t length = TO_LITTLE_ENDIAN_32(1);

  /* Enable write mode */
  writeEnable(memory, nonvolatile);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &length);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, 0);

  ifWrite(memory->spim, &value, 1);

  /* Wait until write operation is completed */
  waitMemoryBusy(memory);
}
/*----------------------------------------------------------------------------*/
void w25MemoryMappingDisable(struct W25SPIM *memory)
{
  ifSetParam(memory->spim, IF_SPIM_INDIRECT, 0);

  if (memory->qpi)
    exitQpiMode(memory);

  busRelease(memory);
}
/*----------------------------------------------------------------------------*/
void w25MemoryMappingEnable(struct W25SPIM *memory)
{
  static const uint32_t address = 0;
  static const uint32_t post = 0xFF; /* Continuous read mode */
  static const uint8_t delay = 2; /* 4 clock cycles on QUAD IO */
  uint8_t command;

  busAcquire(memory);

  if (memory->qpi)
  {
    /* QPI mode must be enabled before command setup */
    enableQpiMode(memory);

    ifSetParam(memory->spim, IF_SPIM_COMMAND_PARALLEL, 0);
  }
  else
  {
    ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, 0);
  }

  if (memory->extended && !memory->shrink)
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_32, &address);
    command = memory->quad ?
        CMD_FAST_READ_QUAD_IO_4BYTE : CMD_FAST_READ_DUAL_IO_4BYTE;
  }
  else
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &address);
    command = memory->quad ? CMD_FAST_READ_QUAD_IO : CMD_FAST_READ_DUAL_IO;
  }

  if (memory->quad)
    ifSetParam(memory->spim, IF_SPIM_DELAY_LENGTH, &delay);
  else
    ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, 0);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_8, &post);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, 0);

  ifSetParam(memory->spim, IF_SPIM_ADDRESS_PARALLEL, 0);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_PARALLEL, 0);
  ifSetParam(memory->spim, IF_SPIM_DELAY_PARALLEL, 0);
  ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, 0);

  ifSetParam(memory->spim, IF_SPIM_MEMORY_MAPPED, 0);
}
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *object, const void *configBase)
{
  const struct W25SPIMConfig * const config = configBase;
  assert(config != NULL);
  assert(config->spim != NULL);

  struct W25SPIM * const memory = object;
  struct JedecInfo info;
  enum Result res = E_OK;

  memory->callback = NULL;

  memory->spim = config->spim;
  memory->position = 0;
  memory->blocking = true;
  memory->extended = false;
  memory->dtr = false;
  memory->qpi = false;
  memory->shrink = config->shrink;

  memory->context.buffer = 0;
  memory->context.left = 0;
  memory->context.length = 0;
  memory->context.position = 0;
  memory->context.state = STATE_IDLE;

  /* Lock the interface */
  ifSetParam(memory->spim, IF_ACQUIRE, 0);
  /* Detect interface capabilities */
  memory->quad = ifSetParam(memory->spim, IF_SPIM_QUAD, 0) == E_OK;
  /* Read device information */
  info = readJedecInfo(memory);
  /* Unlock the interface */
  ifSetParam(memory->spim, IF_RELEASE, 0);

  if (info.manufacturer != JEDEC_MANUFACTURER_WINBOND)
    return E_DEVICE;

  if (info.type == JEDEC_DEVICE_IM_JM)
  {
    /* QPI and DTR modes are supported on IM and JM parts */
    if (memory->quad)
    {
      if (config->qpi)
        memory->qpi = true;

      if (config->dtr)
        memory->dtr = ifSetParam(memory->spim, IF_SPIM_DDR, 0) == E_OK;
    }
  }
  else if (info.type != JEDEC_DEVICE_IN_IQ_JQ)
  {
    return E_DEVICE;
  }

  switch (info.capacity)
  {
    case JEDEC_CAPACITY_W25Q016:
      memory->capacity = 2 * 1024 * 1024;
      break;

    case JEDEC_CAPACITY_W25Q032:
      memory->capacity = 4 * 1024 * 1024;
      break;

    case JEDEC_CAPACITY_W25Q064:
      memory->capacity = 8 * 1024 * 1024;
      break;

    case JEDEC_CAPACITY_W25Q128:
      memory->capacity = 16 * 1024 * 1024;
      break;

    case JEDEC_CAPACITY_W25Q256:
      memory->capacity = 32 * 1024 * 1024;
      break;

    case JEDEC_CAPACITY_W25Q512:
      memory->capacity = 64 * 1024 * 1024;
      break;

    case JEDEC_CAPACITY_W25Q00:
      memory->capacity = 128 * 1024 * 1024;
      break;

    default:
      return E_DEVICE;
  }

  if (memory->capacity > (1UL << 24))
    memory->extended = true;

  /* Configure memory bus mode */
  busAcquire(memory);
  if (!changeQuadMode(memory, memory->quad))
    res = E_INTERFACE;
  busRelease(memory);

  return res;
}
/*----------------------------------------------------------------------------*/
static void memoryDeinit(void *object __attribute__((unused)))
{
}
/*----------------------------------------------------------------------------*/
static void memorySetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct W25SPIM * const memory = object;

  memory->callbackArgument = argument;
  memory->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result memoryGetParam(void *object, int parameter, void *data)
{
  struct W25SPIM * const memory = object;

  switch ((enum FlashParameter)parameter)
  {
    case IF_FLASH_BLOCK_SIZE:
      *(uint32_t *)data = MEMORY_BLOCK_64KB_SIZE;
      return E_OK;

    case IF_FLASH_SECTOR_SIZE:
      *(uint32_t *)data = MEMORY_SECTOR_4KB_SIZE;
      return E_OK;

    case IF_FLASH_PAGE_SIZE:
      *(uint32_t *)data = MEMORY_PAGE_SIZE;
      return E_OK;

    default:
      break;
  }

  switch ((enum IfParameter)parameter)
  {
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
  struct W25SPIM * const memory = object;

  switch ((enum FlashParameter)parameter)
  {
    case IF_FLASH_ERASE_BLOCK:
    {
      const uint32_t position = *(const uint32_t *)data;

      if (position < memory->capacity)
      {
        if (memory->blocking)
        {
          memory->context.state = STATE_IDLE;

          busAcquire(memory);
          writeEnable(memory, true);
          eraseBlock64KB(memory, position);
          waitMemoryBusy(memory);
          busRelease(memory);

          return E_OK;
        }
        else
        {
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

      if (position < memory->capacity)
      {
        if (memory->blocking)
        {
          memory->context.state = STATE_IDLE;

          busAcquire(memory);
          writeEnable(memory, true);
          eraseSector4KB(memory, position);
          waitMemoryBusy(memory);
          busRelease(memory);

          return E_OK;
        }
        else
        {
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

    default:
      break;
  }

  switch ((enum IfParameter)parameter)
  {
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
      memory->blocking = false;
      return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static size_t memoryRead(void *object, void *buffer, size_t length)
{
  struct W25SPIM * const memory = object;

  if (memory->blocking)
  {
    busAcquire(memory);
    pageRead(memory, memory->position, buffer, length);
    busRelease(memory);

    memory->context.state = STATE_IDLE;
    memory->position += length;
    if (memory->position >= memory->capacity)
      memory->position = 0;
  }
  else
  {
    memory->context.length = length;
    memory->context.state = STATE_READ_WAIT;

    busAcquire(memory);
    pageRead(memory, memory->position, buffer, length);
  }

  return length;
}
/*----------------------------------------------------------------------------*/
static size_t memoryWrite(void *object, const void *buffer, size_t length)
{
  struct W25SPIM * const memory = object;

  if (memory->blocking)
  {
    const uint8_t *data = buffer;
    uint32_t left = (uint32_t)length;
    uint32_t position = memory->position;

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

    memory->context.state = STATE_IDLE;
    memory->position += length;
    if (memory->position >= memory->capacity)
      memory->position = 0;
  }
  else
  {
    memory->context.buffer = buffer;
    memory->context.left = length;
    memory->context.length = length;
    memory->context.position = memory->position;
    memory->context.state = STATE_WRITE_ENABLE;

    busAcquire(memory);
    writeEnable(memory, true);
  }

  return length;
}
