/*
 * st7735.c
 * Copyright (C) 2016 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/displays/display.h>
#include <dpm/displays/st7735.h>
#include <halm/delay.h>
#include <xcore/bits.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
#define DISPLAY_HEIGHT  160
#define DISPLAY_WIDTH   128
/*----------------------------------------------------------------------------*/
enum DisplayCommand
{
  /* System function commands */
  CMD_NOP       = 0x00,
  CMD_SWRESET   = 0x01,
  CMD_RDDID     = 0x04,
  CMD_RDDST     = 0x09,
  CMD_RDDPM     = 0x0A,
  CMD_RDDMADCTL = 0x0B,
  CMD_RDDCOLMOD = 0x0C,
  CMD_RDDIM     = 0x0D,
  CMD_RDDSM     = 0x0E,
  CMD_SLPIN     = 0x10,
  CMD_SLPOUT    = 0x11,
  CMD_PTLON     = 0x12,
  CMD_NORON     = 0x13,
  CMD_INVOFF    = 0x20,
  CMD_INVON     = 0x21,
  CMD_GAMSET    = 0x26,
  CMD_DISPOFF   = 0x28,
  CMD_DISPON    = 0x29,
  CMD_CASET     = 0x2A,
  CMD_RASET     = 0x2B,
  CMD_RAMWR     = 0x2C,
  CMD_RGBSET    = 0x2D, /* ST7735R only */
  CMD_RAMRD     = 0x2E,
  CMD_PTLAR     = 0x30,
  CMD_TEOFF     = 0x34,
  CMD_TEON      = 0x35,
  CMD_MADCTL    = 0x36,
  CMD_IDMOFF    = 0x38,
  CMD_IDMON     = 0x39,
  CMD_COLMOD    = 0x3A,
  CMD_RDID1     = 0xDA,
  CMD_RDID2     = 0xDB,
  CMD_RDID3     = 0xDC,

  /* Panel function commands */
  CMD_FRMCTR1   = 0xB1,
  CMD_FRMCTR2   = 0xB2,
  CMD_FRMCTR3   = 0xB3,
  CMD_INVCTR    = 0xB4,
  CMD_DISSET5   = 0xB6,
  CMD_PWCTR1    = 0xC0,
  CMD_PWCTR2    = 0xC1,
  CMD_PWCTR3    = 0xC2,
  CMD_PWCTR4    = 0xC3,
  CMD_PWCTR5    = 0xC4,
  CMD_VMCTR1    = 0xC5,
  CMD_VMOFCTR   = 0xC7,
  CMD_WRID2     = 0xD1,
  CMD_WRID3     = 0xD2,
  CMD_NVCTR1    = 0xD9,
  CMD_NVCTR2    = 0xDE,
  CMD_NVCTR3    = 0xDF,
  CMD_GAMCTRP1  = 0xE0,
  CMD_GAMCTRN1  = 0xE1,

  /* Panel function commands for ST7735 */
  CMD_EXTCTRL   = 0xF0,
  CMD_PWCTR6    = 0xFC,
  CMD_VCOM4L    = 0xFF
};
/*----------------------------------------------------------------------------*/
static void deselectChip(struct ST7735 *);
static void selectChip(struct ST7735 *);
static void selectCommandMode(struct ST7735 *);
static void selectDataMode(struct ST7735 *);
static void interruptHandler(void *);
static void loadLUT(struct ST7735 *);
static void sendCommand(struct ST7735 *, enum DisplayCommand);
static void sendData(struct ST7735 *, const uint8_t *, size_t);
static void setOrientation(struct ST7735 *, enum DisplayOrientation);
static void setWindow(struct ST7735 *, const struct DisplayWindow *);
/*----------------------------------------------------------------------------*/
static enum Result displayInit(void *, const void *);
static void displayDeinit(void *);
static void displaySetCallback(void *, void (*)(void *), void *);
static enum Result displayGetParam(void *, int, void *);
static enum Result displaySetParam(void *, int, const void *);
static size_t displayRead(void *, void *, size_t);
static size_t displayWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const ST7735 = &(const struct InterfaceClass){
    .size = sizeof(struct ST7735),
    .init = displayInit,
    .deinit = displayDeinit,

    .setCallback = displaySetCallback,
    .getParam = displayGetParam,
    .setParam = displaySetParam,
    .read = displayRead,
    .write = displayWrite
};
/*----------------------------------------------------------------------------*/
static void deselectChip(struct ST7735 *display)
{
  pinSet(display->cs);
  ifSetParam(display->bus, IF_RELEASE, NULL);
}
/*----------------------------------------------------------------------------*/
static void selectChip(struct ST7735 *display)
{
  ifSetParam(display->bus, IF_ACQUIRE, NULL);
  pinReset(display->cs);
}
/*----------------------------------------------------------------------------*/
static void selectCommandMode(struct ST7735 *display)
{
  pinReset(display->rs);
}
/*----------------------------------------------------------------------------*/
static void selectDataMode(struct ST7735 *display)
{
  pinSet(display->rs);
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct ST7735 * const display = object;

  /* Restore blocking mode */
  ifSetCallback(display->bus, NULL, NULL);
  ifSetParam(display->bus, IF_BLOCKING, NULL);

  /* Release the interface */
  deselectChip(display);

  if (display->callback != NULL)
    display->callback(display->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void loadLUT(struct ST7735 *display)
{
  sendCommand(display, CMD_RGBSET);

  /* Red */
  for (uint8_t color = 0; color < 64; color += 2)
    sendData(display, &color, sizeof(color));

  /* Green */
  for (uint8_t color = 0; color < 64; color += 1)
    sendData(display, &color, sizeof(color));

  /* Blue */
  for (uint8_t color = 0; color < 64; color += 2)
    sendData(display, &color, sizeof(color));
}
/*----------------------------------------------------------------------------*/
static void sendCommand(struct ST7735 *display, enum DisplayCommand address)
{
  const uint8_t buffer = address;

  if (address != CMD_RAMWR)
    display->gramActive = false;

  selectCommandMode(display);
  ifWrite(display->bus, &buffer, sizeof(buffer));
}
/*----------------------------------------------------------------------------*/
static void sendData(struct ST7735 *display, const uint8_t *data, size_t length)
{
  selectDataMode(display);
  ifWrite(display->bus, data, length);
}
/*----------------------------------------------------------------------------*/
static void setOrientation(struct ST7735 *display,
    enum DisplayOrientation orientation)
{
  const uint8_t buffer[] = {0x00, orientation << 6};

  /* Lock the interface */
  selectChip(display);

  sendCommand(display, CMD_MADCTL);
  sendData(display, buffer, sizeof(buffer));

  /* Release the interface */
  deselectChip(display);
}
/*----------------------------------------------------------------------------*/
static void setWindow(struct ST7735 *display,
    const struct DisplayWindow *window)
{
  const uint8_t xBuffer[] = {0x00, window->ax, 0x00, window->bx};
  const uint8_t yBuffer[] = {0x00, window->ay, 0x00, window->by};

  /* Lock the interface */
  selectChip(display);

  sendCommand(display, CMD_CASET);
  sendData(display, xBuffer, sizeof(xBuffer));

  sendCommand(display, CMD_RASET);
  sendData(display, yBuffer, sizeof(yBuffer));

  /* Release the interface */
  deselectChip(display);
}
/*----------------------------------------------------------------------------*/
static enum Result displayInit(void *object, const void *configPtr)
{
  const struct ST7735Config * const config = configPtr;
  assert(config != NULL);
  assert(config->bus != NULL);

  struct ST7735 * const display = object;

  display->reset = pinInit(config->reset);
  if (!pinValid(display->reset))
    return E_VALUE;
  pinOutput(display->reset, true);

  display->cs = pinInit(config->cs);
  if (!pinValid(display->cs))
    return E_VALUE;
  pinOutput(display->cs, true);

  display->rs = pinInit(config->rs);
  if (!pinValid(display->rs))
    return E_VALUE;
  pinOutput(display->rs, false);

  display->callback = 0;
  display->bus = config->bus;
  display->blocking = true;

  /* Reset display */
  pinReset(display->reset);
  mdelay(20);
  pinSet(display->reset);
  mdelay(20);

  /* Enable blocking mode by default */
  ifSetCallback(display->bus, NULL, NULL);
  ifSetParam(display->bus, IF_BLOCKING, NULL);

  /* Start of the initialization */
  selectChip(display);

  /*
   * Some implementations also use undocumented commands 0xB0 and 0xB9.
   * Parameters for 0xB0: {0x3C, 0x01}.
   * Parameters for 0xB9: {0xFF, 0x83, 0x53}.
   */

  static const uint8_t disset5Parameters[] = {0x94, 0x6C, 0x50};
  sendCommand(display, CMD_DISSET5);
  sendData(display, disset5Parameters, sizeof(disset5Parameters));

  static const uint8_t frmctr1Parameters[] =
      {0x00, 0x01, 0x1B, 0x03, 0x01, 0x08, 0x77, 0x89};
  sendCommand(display, CMD_FRMCTR1);
  sendData(display, frmctr1Parameters, sizeof(frmctr1Parameters));

  static const uint8_t gamctrp1Parameters[] = {
      0x50, 0x77, 0x40, 0x08, 0xBF, 0x00, 0x03, 0x0F,
      0x00, 0x01, 0x73, 0x00, 0x72, 0x03, 0xB0, 0x0F,
      0x08, 0x00, 0x0F
  };
  sendCommand(display, CMD_GAMCTRP1);
  sendData(display, gamctrp1Parameters, sizeof(gamctrp1Parameters));

  /* 16-bit pixel */
  static const uint8_t colmodParameters[] = {0x05};
  sendCommand(display, CMD_COLMOD);
  sendData(display, colmodParameters, sizeof(colmodParameters));

  sendCommand(display, CMD_SLPOUT);
  sendCommand(display, CMD_DISPON);

  loadLUT(display);

  /* End of the initialization */
  deselectChip(display);

  display->orientation = DISPLAY_ORIENTATION_NORMAL;
  display->window = (struct DisplayWindow){
      0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1
  };
  setWindow(display, &display->window);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void displayDeinit(void *object __attribute__((unused)))
{
}
/*----------------------------------------------------------------------------*/
static void displaySetCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct ST7735 * const display = object;

  display->callbackArgument = argument;
  display->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result displayGetParam(void *object, int parameter, void *data)
{
  struct ST7735 * const display = object;

  switch ((enum DisplayParameter)parameter)
  {
    case IF_DISPLAY_ORIENTATION:
    {
      *(uint8_t *)data = display->orientation;
      return E_OK;
    }

    case IF_DISPLAY_RESOLUTION:
    {
      struct DisplayResolution * const resolution = data;

      resolution->width = DISPLAY_WIDTH;
      resolution->height = DISPLAY_HEIGHT;
      return E_OK;
    }

    case IF_DISPLAY_WINDOW:
    {
      *(struct DisplayWindow *)data = display->window;
      return E_OK;
    }

    default:
      break;
  }

  switch ((enum IfParameter)parameter)
  {
    case IF_STATUS:
      return ifGetParam(display->bus, IF_STATUS, NULL);

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result displaySetParam(void *object, int parameter,
    const void *data)
{
  struct ST7735 * const display = object;

  switch ((enum DisplayParameter)parameter)
  {
    case IF_DISPLAY_ORIENTATION:
    {
      const enum DisplayOrientation orientation = *(const uint8_t *)data;

      if (orientation < DISPLAY_ORIENTATION_END)
      {
        display->orientation = (uint8_t)orientation;
        setOrientation(display, orientation);
        return E_OK;
      }
      else
        return E_VALUE;
    }

    case IF_DISPLAY_WINDOW:
    {
      const struct DisplayWindow * const window = data;

      if (window->ax < window->bx && window->ay < window->by
          && window->bx < DISPLAY_WIDTH && window->by < DISPLAY_HEIGHT)
      {
        display->window = *window;
        setWindow(display, &display->window);
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
static size_t displayRead(void *object, void *buffer, size_t length)
{
  struct ST7735 * const display = object;
  size_t bytesRead;

  /* Lock the interface */
  selectChip(display);

  if (!display->gramActive)
  {
    sendCommand(display, CMD_RAMRD);
    display->gramActive = true;
  }

  selectDataMode(display);
  bytesRead = ifRead(display->bus, buffer, length);

  /* Release the interface */
  deselectChip(display);

  return bytesRead;
}
/*----------------------------------------------------------------------------*/
static size_t displayWrite(void *object, const void *buffer, size_t length)
{
  struct ST7735 * const display = object;
  size_t bytesWritten;

  /* Lock the interface */
  selectChip(display);

  if (!display->gramActive)
  {
    sendCommand(display, CMD_RAMWR);
    display->gramActive = true;
  }

  selectDataMode(display);

  if (display->blocking)
  {
    bytesWritten = ifWrite(display->bus, buffer, length);

    /* Release the interface */
    deselectChip(display);
  }
  else
  {
    ifSetCallback(display->bus, interruptHandler, display);
    ifSetParam(display->bus, IF_ZEROCOPY, NULL);

    bytesWritten = ifWrite(display->bus, buffer, length);

    if (bytesWritten != length)
    {
      /* Error occurred, restore bus state */
      ifSetCallback(display->bus, NULL, NULL);
      ifSetParam(display->bus, IF_BLOCKING, NULL);

      /* Release the interface */
      deselectChip(display);
    }
  }

  return bytesWritten;
}
