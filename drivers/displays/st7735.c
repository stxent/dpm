/*
 * st7735.c
 * Copyright (C) 2016 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <halm/delay.h>
#include <xcore/bits.h>
#include <dpm/drivers/displays/display.h>
#include <dpm/drivers/displays/st7735.h>
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
static void loadLUT(struct ST7735 *);
static void setOrientation(struct ST7735 *, enum displayOrientation);
static void setWindow(struct ST7735 *, uint8_t, uint8_t, uint8_t, uint8_t);
static inline void sendCommand(struct ST7735 *, enum DisplayCommand);
static inline void sendData(struct ST7735 *, const uint8_t *, size_t);
/*----------------------------------------------------------------------------*/
static enum Result displayInit(void *, const void *);
static void displayDeinit(void *);
static enum Result displaySetCallback(void *, void (*)(void *), void *);
static enum Result displayGetParam(void *, enum IfParameter, void *);
static enum Result displaySetParam(void *, enum IfParameter, const void *);
static size_t displayRead(void *, void *, size_t);
static size_t displayWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
static const struct InterfaceClass displayTable = {
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
const struct InterfaceClass * const ST7735 = &displayTable;
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
static void setOrientation(struct ST7735 *display,
    enum displayOrientation orientation)
{
  const uint8_t buffer[] = {0x00, orientation << 6};

  ifSetParam(display->bus, IF_ACQUIRE, 0);
  pinReset(display->cs);

  sendCommand(display, CMD_MADCTL);
  sendData(display, buffer, sizeof(buffer));

  pinSet(display->cs);
  ifSetParam(display->bus, IF_RELEASE, 0);
}
/*----------------------------------------------------------------------------*/
static void setWindow(struct ST7735 *display, uint8_t x0, uint8_t y0,
    uint8_t x1, uint8_t y1)
{
  const uint8_t xBuffer[] = {0x00, x0, 0x00, x1};
  const uint8_t yBuffer[] = {0x00, y0, 0x00, y1};

  ifSetParam(display->bus, IF_ACQUIRE, 0);
  pinReset(display->cs);

  sendCommand(display, CMD_CASET);
  sendData(display, xBuffer, sizeof(xBuffer));

  sendCommand(display, CMD_RASET);
  sendData(display, yBuffer, sizeof(yBuffer));

  pinSet(display->cs);
  ifSetParam(display->bus, IF_RELEASE, 0);
}
/*----------------------------------------------------------------------------*/
static inline void sendCommand(struct ST7735 *display,
    enum DisplayCommand address)
{
  const uint8_t buffer = address;

  if (address != CMD_RAMWR)
    display->gramActive = false;

  pinReset(display->rs);
  ifWrite(display->bus, &buffer, sizeof(buffer));
}
/*----------------------------------------------------------------------------*/
static inline void sendData(struct ST7735 *display, const uint8_t *data,
    size_t length)
{
  pinSet(display->rs);
  ifWrite(display->bus, data, length);
}
/*----------------------------------------------------------------------------*/
static enum Result displayInit(void *object, const void *configPtr)
{
  const struct ST7735Config * const config = configPtr;
  struct ST7735 * const display = object;

  assert(config->bus);

  display->reset = pinInit(config->reset);
  if (!pinValid(display->reset))
    return E_VALUE;
  pinOutput(display->reset, 1);

  display->cs = pinInit(config->cs);
  if (!pinValid(display->cs))
    return E_VALUE;
  pinOutput(display->cs, 1);

  display->rs = pinInit(config->rs);
  if (!pinValid(display->rs))
    return E_VALUE;
  pinOutput(display->rs, 0);

  display->bus = config->bus;

  /* Reset display */
  pinReset(display->reset);
  udelay(10);
  pinSet(display->reset);
  mdelay(120);

  /* Start of the initialization */
  ifSetParam(display->bus, IF_ACQUIRE, 0);
  pinReset(display->cs);

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
  pinSet(display->cs);
  ifSetParam(display->bus, IF_RELEASE, 0);

  setWindow(display, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void displayDeinit(void *object __attribute__((unused)))
{

}
/*----------------------------------------------------------------------------*/
static enum Result displaySetCallback(void *object __attribute__((unused)),
    void (*callback)(void *) __attribute__((unused)),
    void *argument __attribute__((unused)))
{
  return E_ERROR;
}
/*----------------------------------------------------------------------------*/
static enum Result displayGetParam(void *object, enum IfParameter parameter,
    void *data)
{
  struct ST7735 * const display = object;

  switch ((enum IfDisplayParameter)parameter)
  {
    case IF_DISPLAY_RESOLUTION:
    {
      struct DisplayResolution * const resolution =
          (struct DisplayResolution *)data;

      resolution->width = DISPLAY_WIDTH;
      resolution->height = DISPLAY_HEIGHT;
      return E_OK;
    }

    default:
      break;
  }

  switch (parameter)
  {
    case IF_STATUS:
      return ifGetParam(display->bus, IF_STATUS, 0);

    default:
      return E_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result displaySetParam(void *object, enum IfParameter parameter,
    const void *data)
{
  struct ST7735 * const display = object;

  switch ((enum IfDisplayParameter)parameter)
  {
    case IF_DISPLAY_ORIENTATION:
    {
      const enum displayOrientation orientation = *(const uint32_t *)data;

      if (orientation < DISPLAY_ORIENTATION_END)
      {
        setOrientation(display, orientation);
        return E_OK;
      }
      else
        return E_VALUE;
    }

    case IF_DISPLAY_WINDOW:
    {
      const struct DisplayWindow * const window =
          (const struct DisplayWindow *)data;

      if (window->begin.x < window->end.x && window->begin.y < window->end.y
          && window->end.x < DISPLAY_WIDTH && window->end.y < DISPLAY_HEIGHT)
      {
        setWindow(display, window->begin.x, window->begin.y,
            window->end.x, window->end.y);
        return E_OK;
      }
      else
        return E_VALUE;
    }

    default:
      return E_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static size_t displayRead(void *object, void *buffer, size_t length)
{
  struct ST7735 * const display = object;
  size_t bytesRead;

  ifSetParam(display->bus, IF_ACQUIRE, 0);
  pinReset(display->cs);
  if (!display->gramActive)
  {
    sendCommand(display, CMD_RAMRD);
    display->gramActive = true;
  }

  pinSet(display->rs);
  bytesRead = ifRead(display->bus, buffer, length);
  pinSet(display->cs);
  ifSetParam(display->bus, IF_RELEASE, 0);

  return bytesRead;
}
/*----------------------------------------------------------------------------*/
static size_t displayWrite(void *object, const void *buffer, size_t length)
{
  struct ST7735 * const display = object;
  size_t bytesWritten;

  ifSetParam(display->bus, IF_ACQUIRE, 0);
  pinReset(display->cs);
  if (!display->gramActive)
  {
    sendCommand(display, CMD_RAMWR);
    display->gramActive = true;
  }

  pinSet(display->rs);
  bytesWritten = ifWrite(display->bus, buffer, length);
  pinSet(display->cs);
  ifSetParam(display->bus, IF_RELEASE, 0);

  return bytesWritten;
}
