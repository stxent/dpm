/*
 * mx35_quad.c
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/memory/flash_defs.h>
#include <dpm/memory/mx35.h>
#include <dpm/memory/mx35_defs.h>
#include <dpm/memory/mx35_quad.h>
#include <halm/generic/flash.h>
#include <halm/generic/spim.h>
#include <xcore/memory.h>
#include <assert.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
enum
{
  STATE_IDLE,
  STATE_READ_PAGE_START,
  STATE_READ_PAGE_WAIT,
  STATE_READ_CACHE_WAIT,
  STATE_WRITE_ENABLE,
  STATE_WRITE_CACHE,
  STATE_WRITE_PAGE_START,
  STATE_WRITE_PAGE_WAIT,
  STATE_ERASE_ENABLE,
  STATE_ERASE_START,
  STATE_ERASE_WAIT,
  STATE_ERROR
};

struct [[gnu::packed]] DeviceId
{
  uint8_t manufacturer;
  uint8_t device;
};
/*----------------------------------------------------------------------------*/
static inline uint32_t addressToColumn(const struct MX35Quad *, uint32_t);
static inline uint32_t addressToRow(const struct MX35Quad *, uint32_t);
static void busAcquire(struct MX35Quad *);
static void busRelease(struct MX35Quad *);
static void cacheRead(struct MX35Quad *, uint32_t, void *, size_t);
static void cacheWrite(struct MX35Quad *, uint32_t, const void *, size_t);
static bool changeQuadMode(struct MX35Quad *, bool);
static void contextReset(struct MX35Quad *);
static bool disableBlockProtection(struct MX35Quad *);
static void eraseBlock(struct MX35Quad *, uint32_t);
static void interruptHandler(void *);
static void pageProgram(struct MX35Quad *, uint32_t);
static void pageRead(struct MX35Quad *, uint32_t);
static void pollFeatureRegister(struct MX35Quad *, uint8_t, uint8_t);
static struct DeviceId readDeviceId(struct MX35Quad *);
static uint8_t readFeatureRegister(struct MX35Quad *, uint8_t);
static void waitMemoryBusy(struct MX35Quad *);
static void writeEnable(struct MX35Quad *);
static void writeFeatureRegister(struct MX35Quad *, uint8_t, uint8_t);
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *, const void *);
static void memoryDeinit(void *);
static void memorySetCallback(void *, void (*)(void *), void *);
static enum Result memoryGetParam(void *, int, void *);
static enum Result memorySetParam(void *, int, const void *);
static size_t memoryRead(void *, void *, size_t);
static size_t memoryWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const MX35Quad = &(const struct InterfaceClass){
    .size = sizeof(struct MX35Quad),
    .init = memoryInit,
    .deinit = memoryDeinit,

    .setCallback = memorySetCallback,
    .getParam = memoryGetParam,
    .setParam = memorySetParam,
    .read = memoryRead,
    .write = memoryWrite
};
/*----------------------------------------------------------------------------*/
static inline uint32_t addressToColumn(const struct MX35Quad *memory,
    uint32_t address)
{
  return address % memory->page;
}
/*----------------------------------------------------------------------------*/
static inline uint32_t addressToRow(const struct MX35Quad *memory,
    uint32_t address)
{
  return address / memory->page;
}
/*----------------------------------------------------------------------------*/
static void busAcquire(struct MX35Quad *memory)
{
  ifSetParam(memory->spim, IF_ACQUIRE, NULL);

  ifSetParam(memory->spim, IF_SPIM_MODE, &((uint8_t){0}));
  ifSetParam(memory->spim, memory->quad ? IF_SPIM_QUAD : IF_SPIM_DUAL, NULL);

  if (memory->blocking)
  {
    ifSetParam(memory->spim, IF_BLOCKING, NULL);
    ifSetCallback(memory->spim, NULL, NULL);
  }
  else
  {
    ifSetParam(memory->spim, IF_ZEROCOPY, NULL);
    ifSetCallback(memory->spim, interruptHandler, memory);
  }
}
/*----------------------------------------------------------------------------*/
static void busRelease(struct MX35Quad *memory)
{
  if (!memory->blocking)
    ifSetCallback(memory->spim, NULL, NULL);
  ifSetParam(memory->spim, IF_RELEASE, NULL);
}
/*----------------------------------------------------------------------------*/
static void cacheRead(struct MX35Quad *memory, uint32_t position,
    void *buffer, size_t length)
{
  const uint32_t column = toLittleEndian32(addressToColumn(memory, position));
  const uint32_t count = toLittleEndian32(length);
  [[maybe_unused]] enum Result res;
  uint8_t command;
  uint8_t delay;

  if (memory->quad)
  {
    if (memory->qio)
    {
      command = CMD_READ_FROM_CACHE_QUAD_IO;
      delay = 2; /* 4 clocks, 2 clocks per byte */
    }
    else
    {
      command = CMD_READ_FROM_CACHE_X4;
      delay = 1; /* 8 clocks, 8 clocks per byte */
    }
  }
  else
  {
    if (memory->qio)
    {
      command = CMD_READ_FROM_CACHE_DUAL_IO;
      delay = 1; /* 4 clocks, 4 clocks per byte */
    }
    else
    {
      command = CMD_READ_FROM_CACHE_X2;
      delay = 1; /* 8 clocks, 8 clocks per byte */
    }

  }

  /* Check for non-standard address length support */
  res = ifSetParam(memory->spim, IF_SPIM_ADDRESS_16, &column);
  assert(res == E_OK);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DELAY_LENGTH, &delay);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &count);

  if (memory->qio)
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_PARALLEL, NULL);
    ifSetParam(memory->spim, IF_SPIM_DELAY_PARALLEL, NULL);
  }
  else
  {
    ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
    ifSetParam(memory->spim, IF_SPIM_DELAY_SERIAL, NULL);
  }
  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, NULL);

  ifRead(memory->spim, buffer, length);
}
/*----------------------------------------------------------------------------*/
static void cacheWrite(struct MX35Quad *memory, uint32_t position,
    const void *buffer, size_t length)
{
  const uint32_t column = toLittleEndian32(addressToColumn(memory, position));
  const uint32_t count = toLittleEndian32(length);
  [[maybe_unused]] enum Result res;
  uint8_t command;

  if (memory->quad)
    command = CMD_PROGRAM_LOAD_X4;
  else
    command = CMD_PROGRAM_LOAD;

  /* Check for non-standard address length support */
  res = ifSetParam(memory->spim, IF_SPIM_ADDRESS_16, &column);
  assert(res == E_OK);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &command);
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH, &count);

  if (memory->quad)
    ifSetParam(memory->spim, IF_SPIM_DATA_PARALLEL, NULL);
  else
    ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);

  ifWrite(memory->spim, buffer, length);
}
/*----------------------------------------------------------------------------*/
static bool changeErrorCorrectionMode(struct MX35Quad *memory, bool enabled)
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
static bool changeQuadMode(struct MX35Quad *memory, bool enabled)
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
static void contextReset(struct MX35Quad *memory)
{
  memory->context.buffer = 0;
  memory->context.left = 0;
  memory->context.length = 0;
  memory->context.position = 0;
  memory->context.state = STATE_IDLE;
}
/*----------------------------------------------------------------------------*/
static bool disableBlockProtection(struct MX35Quad *memory)
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
static void eraseBlock(struct MX35Quad *memory, uint32_t position)
{
  const uint32_t row = addressToRow(memory, position);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_BLOCK_ERASE}));
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &row);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, NULL);

  ifWrite(memory->spim, NULL, 0);
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *argument)
{
  struct MX35Quad * const memory = argument;
  const enum Result status = ifGetParam(memory->spim, IF_STATUS, NULL);
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
    case STATE_READ_PAGE_START:
      /* Update context */
      memory->context.state = STATE_READ_PAGE_WAIT;

      /* Poll OIP bit in Status Feature Register */
      pollFeatureRegister(memory, FEATURE_STATUS, 0);
      break;

    case STATE_READ_PAGE_WAIT:
    {
      uint8_t * const data = (uint8_t *)memory->context.buffer;
      const uint32_t position = memory->context.position;
      const uint32_t available = memory->page - position % memory->page;
      const uint32_t chunk = MIN(available, memory->context.left);

      /* Update context */
      memory->context.state = STATE_READ_CACHE_WAIT;
      memory->context.length = chunk;

      cacheRead(memory, position, data, chunk);
      break;
    }

    case STATE_READ_CACHE_WAIT:
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

      /* Update context */
      memory->context.length = chunk;
      memory->context.state = STATE_WRITE_CACHE;

      cacheWrite(memory, position, data, chunk);
      break;
    }

    case STATE_WRITE_CACHE:
      /* Update context */
      memory->context.state = STATE_WRITE_PAGE_START;

      pageProgram(memory, memory->context.position);
      break;

    case STATE_WRITE_PAGE_START:
      /* Update context */
      memory->context.state = STATE_WRITE_PAGE_WAIT;

      /* Poll OIP bit in Status Feature Register */
      pollFeatureRegister(memory, FEATURE_STATUS, 0);
      break;

    case STATE_WRITE_PAGE_WAIT:
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
      break;

    case STATE_ERASE_ENABLE:
      memory->context.state = STATE_ERASE_START;
      eraseBlock(memory, memory->context.position);
      break;

    case STATE_ERASE_START:
      /* Poll OIP bit in Status Feature Register */
      memory->context.state = STATE_ERASE_WAIT;
      pollFeatureRegister(memory, FEATURE_STATUS, 0);
      break;

    case STATE_ERASE_WAIT:
      memory->context.state = STATE_IDLE;
      event = true;

      busRelease(memory);
      break;

    default:
      break;
  }

  if (event && memory->callback != NULL)
    memory->callback(memory->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void pageProgram(struct MX35Quad *memory, uint32_t position)
{
  const uint32_t row = toLittleEndian32(addressToRow(memory, position));

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_PROGRAM_EXECUTE}));
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &row);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, NULL);

  ifWrite(memory->spim, NULL, 0);
}
/*----------------------------------------------------------------------------*/
static void pageRead(struct MX35Quad *memory, uint32_t position)
{
  const uint32_t row = toLittleEndian32(addressToRow(memory, position));

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_PAGE_READ}));
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_24, &row);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, NULL);

  ifWrite(memory->spim, NULL, 0);
}
/*----------------------------------------------------------------------------*/
static void pollFeatureRegister(struct MX35Quad *memory, uint8_t feature,
    uint8_t bit)
{
  const uint32_t address = toLittleEndian32(feature);
  [[maybe_unused]] enum Result res;

  /* Check for non-standard address length support */
  res = ifSetParam(memory->spim, IF_SPIM_ADDRESS_8, &address);
  assert(res == E_OK);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_GET_FEATURE}));
  ifSetParam(memory->spim, IF_SPIM_DATA_POLL_BIT, &bit);

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

  ifRead(memory->spim, NULL, 0);
}
/*----------------------------------------------------------------------------*/
static struct DeviceId readDeviceId(struct MX35Quad *memory)
{
  struct DeviceId info;

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_READ_ID}));
  /* 8 clocks */
  ifSetParam(memory->spim, IF_SPIM_DELAY_LENGTH, &((uint8_t){1}));
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH,
      &((uint32_t){TO_LITTLE_ENDIAN_32(sizeof(struct DeviceId))}));

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

  ifRead(memory->spim, &info, sizeof(info));
  return info;
}
/*----------------------------------------------------------------------------*/
static uint8_t readFeatureRegister(struct MX35Quad *memory, uint8_t feature)
{
  const uint32_t address = toLittleEndian32(feature);
  [[maybe_unused]] enum Result res;
  uint8_t data;

  /* Check for non-standard address length support */
  res = ifSetParam(memory->spim, IF_SPIM_ADDRESS_8, &address);
  assert(res == E_OK);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_GET_FEATURE}));
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH,
      &((uint32_t){TO_LITTLE_ENDIAN_32(1)}));

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

  ifRead(memory->spim, &data, 1);
  return data;
}
/*----------------------------------------------------------------------------*/
static void waitMemoryBusy(struct MX35Quad *memory)
{
  uint8_t status;

  do
  {
    status = readFeatureRegister(memory, FEATURE_STATUS);
  }
  while (status & FR_STATUS_OIP);
}
/*----------------------------------------------------------------------------*/
static void writeEnable(struct MX35Quad *memory)
{
  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_WRITE_ENABLE}));

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_NONE, NULL);

  ifWrite(memory->spim, NULL, 0);
}
/*----------------------------------------------------------------------------*/
static void writeFeatureRegister(struct MX35Quad *memory, uint8_t feature,
    uint8_t value)
{
  const uint32_t address = toLittleEndian32(feature);
  [[maybe_unused]] enum Result res;

  /* Enable write mode */
  writeEnable(memory);

  /* Check for non-standard address length support */
  res = ifSetParam(memory->spim, IF_SPIM_ADDRESS_8, &address);
  assert(res == E_OK);

  ifSetParam(memory->spim, IF_SPIM_COMMAND, &((uint8_t){CMD_SET_FEATURE}));
  ifSetParam(memory->spim, IF_SPIM_DATA_LENGTH,
      &((uint32_t){TO_LITTLE_ENDIAN_32(1)}));

  ifSetParam(memory->spim, IF_SPIM_COMMAND_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_ADDRESS_SERIAL, NULL);
  ifSetParam(memory->spim, IF_SPIM_POST_ADDRESS_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DELAY_NONE, NULL);
  ifSetParam(memory->spim, IF_SPIM_DATA_SERIAL, NULL);

  ifWrite(memory->spim, &value, 1);

  /* Wait until write operation is completed */
  waitMemoryBusy(memory);
}
/*----------------------------------------------------------------------------*/
static enum Result memoryInit(void *object, const void *configBase)
{
  const struct MX35QuadConfig * const config = configBase;
  assert(config != NULL);
  assert(config->spim != NULL);

  struct MX35Quad * const memory = object;
  enum Result res = E_OK;

  memory->callback = NULL;
  memory->spim = config->spim;
  memory->position = 0;
  memory->blocking = true;
  memory->ecc = config->ecc;
  contextReset(memory);

  /* Lock the interface */
  busAcquire(memory);
  /* Explicitly enter indirect mode */
  ifSetParam(memory->spim, IF_SPIM_INDIRECT, NULL);
  /* Detect interface capabilities */
  memory->quad = ifSetParam(memory->spim, IF_SPIM_QUAD, NULL) == E_OK;
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
  memory->qio = info.qio;

  busAcquire(memory);
  /* Configure memory bus mode */
  if (!changeQuadMode(memory, memory->quad))
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
static void memoryDeinit(void *)
{
}
/*----------------------------------------------------------------------------*/
static void memorySetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct MX35Quad * const memory = object;

  memory->callbackArgument = argument;
  memory->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result memoryGetParam(void *object, int parameter, void *data)
{
  struct MX35Quad * const memory = object;

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
  struct MX35Quad * const memory = object;

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
  struct MX35Quad * const memory = object;

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
  struct MX35Quad * const memory = object;

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
