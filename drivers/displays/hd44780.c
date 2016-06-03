/*
 * hd44780.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <xcore/bits.h>
#include <dpm/drivers/displays/hd44780.h>
/*----------------------------------------------------------------------------*/
/*
 * Entry Mode Set: 0 0 0 0  0 1 I/D S/H
 * When I/D set, cursor moves to right and DDRAM address is increased by 1.
 * When S/H set, shift of entire display is performed.
 */
#define HD44780_ENTRY_MODE              0x04
#define HD44780_SHIFT_CURSOR            0
#define HD44780_SHIFT_DISPLAY           BIT(0)
#define HD44780_SHIFT_LEFT              0
#define HD44780_SHIFT_RIGHT             BIT(1)

/* Display On/Off Control: 0 0 0 0  1 D C B */
#define HD44780_CONTROL                 0x08
#define HD44780_BLINK_ON                BIT(0)
#define HD44780_CURSOR_ON               BIT(1)
#define HD44780_DISPLAY_ON              BIT(2)

/* Cursor or Display shift: 0 0 0 1  S/C R/L - - */
#define HD44780_SHIFT                   0x10
#define HD44780_CURSOR_SHIFT_LEFT       BIT_FIELD(0, 2)
#define HD44780_CURSOR_SHIFT_RIGHT      BIT_FIELD(1, 2)
#define HD44780_DISPLAY_SHIFT_LEFT      BIT_FIELD(2, 2)
#define HD44780_DISPLAY_SHIFT_RIGHT     BIT_FIELD(3, 2)

/* Function Set: 0 0 1 DL  N F - - */
#define HD44780_FUNCTION                0x20
#define HD44780_FONT_5x8                0
#define HD44780_FONT_5x11               BIT(2)
#define HD44780_2_LINES                 BIT(3)
#define HD44780_BUS_4BIT                0
#define HD44780_BUS_8BIT                BIT(4)

/* Set position */
#define HD44780_POSITION                0x80
/*----------------------------------------------------------------------------*/
enum state
{
  STATE_IDLE,
  STATE_RESET,
  STATE_WRITE_ADDRESS,
  STATE_WRITE_DATA
};
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *);
static void setPosition(struct HD44780 *, struct DisplayPoint);
static void updateDisplay(struct HD44780 *);
/*----------------------------------------------------------------------------*/
static enum result displayInit(void *, const void *);
static void displayDeinit(void *);
static enum result displayCallback(void *, void (*)(void *), void *);
static enum result displayGet(void *, enum ifOption, void *);
static enum result displaySet(void *, enum ifOption, const void *);
static size_t displayRead(void *, void *, size_t);
static size_t displayWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
static const struct InterfaceClass displayTable = {
    .size = sizeof(struct HD44780),
    .init = displayInit,
    .deinit = displayDeinit,

    .callback = displayCallback,
    .get = displayGet,
    .set = displaySet,
    .read = displayRead,
    .write = displayWrite
};
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const HD44780 = &displayTable;
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct HD44780 * const display = object;

  switch (display->state)
  {
    case STATE_WRITE_ADDRESS:
    {
      const unsigned int offset = display->line * display->resolution.width;

      display->state = STATE_WRITE_DATA;
      pinSet(display->rs);
      ifWrite(display->bus, display->buffer + offset,
          display->resolution.width);
      break;
    }

    case STATE_WRITE_DATA:
    {
      if (display->line < display->resolution.height - 1)
      {
        ++display->line;

        display->state = STATE_WRITE_ADDRESS;
        setPosition(display, (struct DisplayPoint){0, display->line});
      }
      else
        display->state = STATE_IDLE;
      break;
    }

    case STATE_RESET:
    {
      display->state = STATE_IDLE;
      break;
    }

    default:
      break;
  }

  if (display->state == STATE_IDLE)
  {
    if (display->update)
    {
      display->update = false;
      updateDisplay(display);
    }
    else
    {
      if (display->callback)
        display->callback(display->callbackArgument);
    }
  }
}
/*----------------------------------------------------------------------------*/
static void setPosition(struct HD44780 *display, struct DisplayPoint position)
{
  pinReset(display->rs);

  /* Set DDRAM address */
  display->command[0] = HD44780_POSITION | position.x;

  if (position.y)
    display->command[0] |= 0x40;

  ifWrite(display->bus, display->command, 1);
}
/*----------------------------------------------------------------------------*/
static void updateDisplay(struct HD44780 *display)
{
  display->line = 0;
  display->state = STATE_WRITE_ADDRESS;
  setPosition(display, (struct DisplayPoint){0, 0});
}
/*----------------------------------------------------------------------------*/
static enum result displayInit(void *object, const void *configPtr)
{
  const struct HD44780Config * const config = configPtr;
  struct HD44780 * const display = object;
  enum result res;

  const size_t bufferSize =
      config->resolution.width * config->resolution.height;

  assert(config->bus);
  assert(config->resolution.width && config->resolution.height);

  if ((res = ifSet(config->bus, IF_ZEROCOPY, 0)) != E_OK)
    return res;
  if ((res = ifCallback(config->bus, interruptHandler, display)) != E_OK)
    return res;

  display->rs = pinInit(config->rs);
  if (!pinValid(display->rs))
    return E_VALUE;
  /* Initialize and select instruction registers */
  pinOutput(display->rs, 0);

  if (!(display->buffer = malloc(bufferSize)))
    return E_MEMORY;
  memset(display->buffer, ' ', bufferSize);

  display->callback = 0;
  display->bus = config->bus;
  display->line = 0;
  display->state = STATE_RESET;
  display->update = true;

  display->resolution = config->resolution;
  display->window = (struct DisplayWindow){
      {0, 0},
      {display->resolution.width - 1, display->resolution.height - 1}
  };

  /* Function set */
  display->command[0] = HD44780_FUNCTION | HD44780_BUS_8BIT
      | HD44780_2_LINES | HD44780_FONT_5x8;
  /* Display On/Off control */
  display->command[1] = HD44780_CONTROL | HD44780_DISPLAY_ON
      | HD44780_CURSOR_ON | HD44780_BLINK_ON;
  /* Entry mode set */
  display->command[2] = HD44780_ENTRY_MODE | HD44780_SHIFT_RIGHT;
  /* Cursor or display shift */
  display->command[3] = HD44780_SHIFT | HD44780_CURSOR_SHIFT_LEFT;

  ifWrite(display->bus, display->command, 4);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void displayDeinit(void *object)
{
  struct HD44780 * const display = object;

  free(display->buffer);
}
/*----------------------------------------------------------------------------*/
static enum result displayCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct HD44780 * const display = object;

  display->callbackArgument = argument;
  display->callback = callback;
  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum result displayGet(void *object, enum ifOption option, void *data)
{
  const struct HD44780 * const display = object;

  switch ((enum ifDisplayOption)option)
  {
    case IF_DISPLAY_RESOLUTION:
      *(struct DisplayResolution *)data = display->resolution;
      return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum result displaySet(void *object, enum ifOption option,
    const void *data)
{
  struct HD44780 * const display = object;

  switch ((enum ifDisplayOption)option)
  {
    case IF_DISPLAY_WINDOW:
    {
      const struct DisplayWindow * const window =
          (const struct DisplayWindow *)data;

      if (window->begin.x < window->end.x && window->begin.y < window->end.y
          && window->end.x < display->resolution.width
          && window->end.y < display->resolution.height)
      {
        display->window = *window;
        return E_OK;
      }
      else
        return E_VALUE;
    }

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static size_t displayRead(void *object __attribute__((unused)),
    void *buffer __attribute__((unused)),
    size_t length __attribute__((unused)))
{
  return 0;
}
/*----------------------------------------------------------------------------*/
static size_t displayWrite(void *object, const void *buffer, size_t length)
{
  struct HD44780 * const display = object;
  const size_t sourceLength = length;
  const uint8_t *bufferPosition = buffer;

  for (unsigned int row = display->window.begin.y;
      length && row <= display->window.end.y; ++row)
  {
    const size_t offset = row * display->resolution.width
        + display->window.begin.x;
    size_t bytesToWrite = display->window.end.x - display->window.begin.x;

    if (bytesToWrite > length)
      bytesToWrite = length;

    memcpy(display->buffer + offset, bufferPosition, bytesToWrite);
    bufferPosition += bytesToWrite;
    length -= bytesToWrite;
  }

  if (display->state == STATE_IDLE)
    updateDisplay(display);
  else
    display->update = true;

  return sourceLength - length;
}
