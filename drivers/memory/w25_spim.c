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
static bool changeDriverStrength(struct W25SPIM *, enum W25DriverStrength);
static bool changeQuadMode(struct W25SPIM *, bool);
static void eraseBlock64KB(struct W25SPIM *, uint32_t);
static void eraseSector4KB(struct W25SPIM *, uint32_t);
static void exitQpiXipMode(struct W25SPIM *);
static void interruptHandler(void *);
static void makeReadCommandValues(const struct W25SPIM *, uint8_t *, uint8_t *);
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
  ifSetParam(memory->spim, IF_ACQUIRE, NULL);
  ifSetParam(memory->spim, memory->quad ? IF_SPIM_QUAD : IF_SPIM_DUAL, NULL);

  if (!memory->blocking)
  {
    ifSetParam(memory->spim, IF_ZEROCOPY, NULL);
    ifSetCallback(memory->spim, interruptHandler, memory);
  }
  else
    ifSetParam(memory->spim, IF_BLOCKING, NULL);
}
/*----------------------------------------------------------------------------*/
static void busRelease(struct W25SPIM *memory)
{
  if (!memory->blocking)
    ifSetCallback(memory->spim, NULL, NULL);
  ifSetParam(memory->spim, IF_RELEASE, NULL);
}
/*----------------------------------------------------------------------------*/
static bool changeDriverStrength(struct W25SPIM *memory,
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

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, NULL);

  ifWrite(memory->spim, NULL, 0);
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

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, NULL);

  ifWrite(memory->spim, NULL, 0);
}
/*----------------------------------------------------------------------------*/
static void exitQpiXipMode(struct W25SPIM *memory)
{
  static const uint8_t pattern[] = {
      XIP_MODE_EXIT, XIP_MODE_EXIT, XIP_MODE_EXIT, XIP_MODE_EXIT
  };

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &(uint8_t){XIP_MODE_EXIT});

  ifSetParam(memory->spim, IF_SPIM_COMMAND_PARALLEL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, NULL);

  ifWrite(memory->spim, pattern, sizeof(pattern));
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *argument)
{
  struct W25SPIM * const memory = argument;
  const enum Result status = ifGetParam(memory->spim, IF_STATUS, NULL);
  bool event = false;

  assert(memory->context.state != STATE_IDLE
      && memory->context.state != STATE_ERROR);

  if (status != E_OK)
  {
    memory->context.state = STATE_ERROR;
    event = true;

    memory->context.buffer = NULL;
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

      if (memory->dtr)
        ifSetParam(memory->spim, IF_SPIM_SDR, NULL);

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

        memory->context.buffer = NULL;
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

  if (event && memory->callback != NULL)
    memory->callback(memory->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void makeReadCommandValues(const struct W25SPIM *memory,
    uint8_t *command, uint8_t *delay)
{
  if (memory->extended && !memory->shrink)
  {
    if (memory->quad)
    {
      *command = CMD_FAST_READ_QUAD_IO_4BYTE;
      *delay = 2; /* 4 clocks, 2 clocks per byte */
    }
    else
    {
      *command = CMD_FAST_READ_DUAL_IO_4BYTE;
      *delay = 0;
    }
  }
  else
  {
    if (memory->quad)
    {
      if (memory->dtr)
      {
        *command = CMD_FAST_READ_QUAD_IO_DTR;
        *delay = 7; /* 7 clocks, 1 clock per byte */
      }
      else
      {
        *command = CMD_FAST_READ_QUAD_IO;
        *delay = 2; /* 4 clocks when not in QPI mode, 2 clocks per byte */
      }
    }
    else
    {
      if (memory->dtr)
      {
        *command = CMD_FAST_READ_DUAL_IO_DTR;
        *delay = 2; /* 4 clocks, 2 clocks per byte */
      }
      else
      {
        *command = CMD_FAST_READ_DUAL_IO;
        *delay = 0;
      }
    }
  }
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
    ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, NULL);
  else
    ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &count);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);

  ifWrite(memory->spim, buffer, length);
}
/*----------------------------------------------------------------------------*/
static void pageRead(struct W25SPIM *memory, uint32_t position,
    void *buffer, size_t length)
{
  const uint32_t address = toLittleEndian32(position);
  const uint32_t count = toLittleEndian32(length);
  uint8_t command;
  uint8_t delay;

  makeReadCommandValues(memory, &command, &delay);

  if (memory->extended)
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_32, &address);
  else
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &address);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_8, &((uint8_t){0xFF}));
  ifSetParam(memory->spim, IF_SPIM_DELAY_LENGTH, &delay);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &count);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_PARALLEL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_PARALLEL, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_PARALLEL, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, NULL);

  if (memory->dtr)
  {
    /* Enable DDR mode */
    ifSetParam(memory->spim, IF_SPIM_DDR, NULL);
  }

  ifRead(memory->spim, buffer, length);

  if (memory->dtr && memory->blocking)
  {
    /* Disable DDR mode */
    ifSetParam(memory->spim, IF_SPIM_SDR, NULL);
  }
}
/*----------------------------------------------------------------------------*/
static void pollStatusRegister(struct W25SPIM *memory, uint8_t command,
    uint8_t bit)
{
  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_POLL_BIT, &bit);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

  ifRead(memory->spim, NULL, 0);
}
/*----------------------------------------------------------------------------*/
static struct JedecInfo readJedecInfo(struct W25SPIM *memory)
{
  struct JedecInfo info;

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_READ_JEDEC_ID}));
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH,
      &((uint32_t){TO_LITTLE_ENDIAN_32(sizeof(struct JedecInfo))}));

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

  ifRead(memory->spim, &info, sizeof(info));
  return info;
}
/*----------------------------------------------------------------------------*/
static uint8_t readStatusRegister(struct W25SPIM *memory, uint8_t command)
{
  uint8_t data;

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH,
      &((uint32_t){TO_LITTLE_ENDIAN_32(1)}));

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

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

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, NULL);

  ifWrite(memory->spim, NULL, 0);
}
/*----------------------------------------------------------------------------*/
static void writeStatusRegister(struct W25SPIM *memory, uint8_t command,
    uint8_t value, bool nonvolatile)
{
  /* Enable write mode */
  writeEnable(memory, nonvolatile);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH,
      &((uint32_t){TO_LITTLE_ENDIAN_32(1)}));

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

  ifWrite(memory->spim, &value, 1);

  /* Wait until write operation is completed */
  waitMemoryBusy(memory);
}
/*----------------------------------------------------------------------------*/
void w25MemoryMappingDisable(struct W25SPIM *memory)
{
  ifSetParam(memory->spim, IF_SPIM_INDIRECT, NULL);

  if (memory->xip)
  {
    /* Change control byte */
    ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_8,
        &((uint8_t){XIP_MODE_EXIT}));

    /* Issue mock read command to exit no-opcode mode */
    ifRead(memory->spim, NULL, 0);
  }

  if (memory->dtr)
  {
    /* Disable DDR mode */
    ifSetParam(memory->spim, IF_SPIM_SDR, NULL);
  }

  busRelease(memory);
}
/*----------------------------------------------------------------------------*/
void w25MemoryMappingEnable(struct W25SPIM *memory)
{
  const uint8_t post = memory->xip ? XIP_MODE_ENTER : XIP_MODE_EXIT;
  uint8_t command;
  uint8_t delay;

  assert(memory->blocking);
  busAcquire(memory);

  makeReadCommandValues(memory, &command, &delay);

  if (memory->extended && !memory->shrink)
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_32, &((uint32_t){0}));
  else
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &((uint32_t){0}));

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_8, &post);
  ifSetParam(memory->spim, IF_SPIM_DELAY_LENGTH, &delay);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, NULL);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_PARALLEL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_PARALLEL, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_PARALLEL, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, NULL);

  if (memory->dtr)
  {
    /* Enable DDR mode before mock read command */
    ifSetParam(memory->spim, IF_SPIM_DDR, NULL);
  }
  if (memory->xip)
  {
    /* Issue mock read command to enter Continuous Read mode */
    ifRead(memory->spim, NULL, 0);
    /* Switch to no-opcode mode */
    ifSetParam(memory->spim, IF_SPIM_COMMAND_NONE, NULL);
  }

  ifSetParam(memory->spim, IF_SPIM_MEMORY_MAPPED, NULL);
}
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *object, const void *configBase)
{
  const struct W25SPIMConfig * const config = configBase;
  assert(config != NULL);
  assert(config->spim != NULL);
  assert(config->strength < W25_DRV_END);

  struct W25SPIM * const memory = object;
  struct JedecInfo info;
  enum Result res = E_OK;

  memory->callback = NULL;
  memory->spim = config->spim;

  memory->position = 0;
  memory->blocking = true;
  memory->dtr = false;
  memory->extended = false;
  memory->shrink = config->shrink;
  memory->xip = false;

  memory->context.buffer = NULL;
  memory->context.left = 0;
  memory->context.length = 0;
  memory->context.position = 0;
  memory->context.state = STATE_IDLE;

  /* Lock the interface */
  ifSetParam(memory->spim, IF_ACQUIRE, NULL);
  /* Detect interface capabilities */
  memory->quad = ifSetParam(memory->spim, IF_SPIM_QUAD, NULL) == E_OK;
  /* Reset interface mode on the memory side */
  exitQpiXipMode(memory);
  /* Read device information */
  info = readJedecInfo(memory);
  /* Unlock the interface */
  ifSetParam(memory->spim, IF_RELEASE, NULL);

  if (info.manufacturer != JEDEC_MANUFACTURER_WINBOND)
    return E_DEVICE;

  if (info.type == JEDEC_DEVICE_IM_JM)
  {
    /* DTR, QPI and XIP modes are supported only on IM and JM parts */

    if (config->dtr)
    {
      if (ifSetParam(memory->spim, IF_SPIM_DDR, NULL) == E_OK)
      {
        /* Restore SDR mode */
        ifSetParam(memory->spim, IF_SPIM_SDR, NULL);

        memory->dtr = true;
      }
      else
      {
        /* Peripheral interface does not support DDR mode */
        return E_INTERFACE;
      }
    }

    if (config->xip)
      memory->xip = true;
  }
  else if (info.type == JEDEC_DEVICE_IN_IQ_JQ)
  {
    if (config->dtr || config->xip)
    {
      /* Memory chip does not support DTR and XIP modes */
      return E_DEVICE;
    }
  }
  else
  {
    /* Unsupported memory chip */
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

    case JEDEC_CAPACITY_W25Q01:
      memory->capacity = 128 * 1024 * 1024;
      break;

    case JEDEC_CAPACITY_W25Q02:
      memory->capacity = 256 * 1024 * 1024;
      break;

    default:
      /* Unsupported capacity code */
      return E_DEVICE;
  }

  if (memory->capacity > (1UL << 24))
    memory->extended = true;

  busAcquire(memory);
  /* Configure memory bus mode */
  if (!changeQuadMode(memory, memory->quad))
    res = E_INTERFACE;
  /* Configure driver strength */
  if (!changeDriverStrength(memory, config->strength))
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
