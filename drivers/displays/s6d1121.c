/*
 * s6d1121.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <bits.h>
#include <delay.h> //FIXME
#include <drivers/displays/display.h>
#include <drivers/displays/s6d1121.h>
/*----------------------------------------------------------------------------*/
#define DISPLAY_HEIGHT  320
#define DISPLAY_WIDTH   240
/*----------------------------------------------------------------------------*/
enum displayRegister
{
  REG_PRODUCTION_CODE                 = 0x00,
  REG_DRIVER_OUTPUT_CONTROL           = 0x01,
  REG_LCD_DRIVING_WAVEFORM_CONTROL    = 0x02,
  REG_ENTRY_MODE                      = 0x03,
  REG_OSCILLATOR_CONTROL              = 0x04,
  REG_DISPLAY_CONTROL                 = 0x07,
  REG_BLANK_PERIOD_CONTROL_1          = 0x08,
  REG_FRAME_CYCLE_CONTROL_1           = 0x0A,
  REG_FRAME_CYCLE_CONTROL             = 0x0B,
  REG_EXTERNAL_INTERFACE_CONTROL      = 0x0C,
  REG_POWER_CONTROL_1                 = 0x10,
  REG_POWER_CONTROL_2                 = 0x11,
  REG_POWER_CONTROL_3                 = 0x12,
  REG_POWER_CONTROL_4                 = 0x13,
  REG_POWER_CONTROL_5                 = 0x14,
  REG_POWER_CONTROL_6                 = 0x15,
  REG_POWER_CONTROL_7                 = 0x16,
  REG_GRAM_ADDRESS_X                  = 0x20,
  REG_GRAM_ADDRESS_Y                  = 0x21,
  REG_GRAM_DATA                       = 0x22,
  REG_GAMMA_CONTROL_1                 = 0x30,
  REG_GAMMA_CONTROL_2                 = 0x31,
  REG_GAMMA_CONTROL_3                 = 0x32,
  REG_GAMMA_CONTROL_4                 = 0x33,
  REG_GAMMA_CONTROL_5                 = 0x34,
  REG_GAMMA_CONTROL_6                 = 0x35,
  REG_GAMMA_CONTROL_7                 = 0x36,
  REG_GAMMA_CONTROL_8                 = 0x37,
  REG_GAMMA_CONTROL_9                 = 0x38,
  REG_GAMMA_CONTROL_10                = 0x39,
  REG_GAMMA_CONTROL_11                = 0x3A,
  REG_GAMMA_CONTROL_12                = 0x3B,
  REG_GAMMA_CONTROL_13                = 0x3C,
  REG_GAMMA_CONTROL_14                = 0x3D,
  REG_VERTICAL_SCROLL_CONTROL         = 0x41,
  /* Screen driving positions skipped */
  REG_HORIZONTAL_WINDOW_ADDRESS       = 0x46,
  REG_VERTICAL_WINDOW_ADDRESS_END     = 0x47,
  REG_VERTICAL_WINDOW_ADDRESS_BEGIN   = 0x48,
  REG_MDDI_WAKEUP_CONTROL             = 0x50,
  REG_MDDI_LINK_WAKEUP_START_POSITION = 0x51,
  REG_SUB_PANEL_CONTROL_1             = 0x52,
  REG_SUB_PANEL_CONTROL_2             = 0x53,
  REG_SUB_PANEL_CONTROL_3             = 0x54,
  /* GPIO registers skipped */
  REG_MTP_INIT                        = 0x60,
  /* Other MTP registers skipped */
  REG_GOE_SIGNAL_TIMING               = 0x70,
  REG_GATE_START_PULSE_DELAY_TIMING   = 0x71,
  REG_RED_OUTPUT_START_TIMING         = 0x72,
  REG_GREEN_OUTPUT_START_TIMING       = 0x73,
  REG_BLUE_OUTPUT_START_TIMING        = 0x74,
  REG_RSW_TIMING                      = 0x75,
  REG_GSW_TIMING                      = 0x76,
  REG_BSW_TIMING                      = 0x77,
  REG_VCOM_OUTPUT_CONTROL             = 0x78,
  REG_PANEL_SIGNAL_CONTROL_1          = 0x79,
  REG_PANEL_SIGNAL_CONTROL_2          = 0x7A,

  /* System definitions */
  DELAY_MS                          = 0xFF
};
/*----------------------------------------------------------------------------*/
struct DisplayPoint
{
  uint16_t x;
  uint16_t y;
};

struct InitEntry
{
  uint16_t address;
  uint16_t value;
};
/*----------------------------------------------------------------------------*/
static struct DisplayPoint addressToPoint(uint32_t);
static void interruptHandler(void *);
static void setPosition(struct S6D1121 *, uint16_t, uint16_t);
static void setWindow(struct S6D1121 *, uint16_t, uint16_t, uint16_t, uint16_t);
static inline void writeAddress(struct S6D1121 *, enum displayRegister);
static inline void writeData(struct S6D1121 *, uint16_t);
static void writeRegister(struct S6D1121 *, enum displayRegister, uint16_t);
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
    .size = sizeof(struct S6D1121),
    .init = displayInit,
    .deinit = displayDeinit,

    .callback = displayCallback,
    .get = displayGet,
    .set = displaySet,
    .read = displayRead,
    .write = displayWrite
};
/*----------------------------------------------------------------------------*/
const struct InterfaceClass *S6D1121 = &displayTable;
/*----------------------------------------------------------------------------*/
static const struct InitEntry initSequence[] = {
    // TODO Delays
    {REG_POWER_CONTROL_2,   0x2004},
    {REG_POWER_CONTROL_4,   0xCC00},
    {REG_POWER_CONTROL_6,   0x2600},
    {REG_POWER_CONTROL_5,   0x252A},
    {REG_POWER_CONTROL_3,   0x0033},
    {REG_POWER_CONTROL_4,   0xCC04},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4,   0xCC06},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4,   0xCC4F},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4,   0x674F},
    {REG_POWER_CONTROL_2,   0x2003},
    {DELAY_MS, 1},

    /* Gamma settings */
    {REG_GAMMA_CONTROL_1,   0x2609},
    {REG_GAMMA_CONTROL_2,   0x242C},
    {REG_GAMMA_CONTROL_3,   0x1F23},
    {REG_GAMMA_CONTROL_4,   0x2425},
    {REG_GAMMA_CONTROL_5,   0x2226},
    {REG_GAMMA_CONTROL_6,   0x2523},
    {REG_GAMMA_CONTROL_7,   0x1C1A},
    {REG_GAMMA_CONTROL_8,   0x131D},
    {REG_GAMMA_CONTROL_9,   0x0B11},
    {REG_GAMMA_CONTROL_10,  0x1210},
    {REG_GAMMA_CONTROL_11,  0x1315},
    {REG_GAMMA_CONTROL_12,  0x3619},
    {REG_GAMMA_CONTROL_13,  0x0D00},
    {REG_GAMMA_CONTROL_14,  0x000D},

    {REG_POWER_CONTROL_7,   0x0007},
    {REG_LCD_DRIVING_WAVEFORM_CONTROL,  0x0013},
    {REG_ENTRY_MODE,                    0x0003},
    {REG_DRIVER_OUTPUT_CONTROL,         0x0127},
    {DELAY_MS, 1},
    {REG_BLANK_PERIOD_CONTROL_1,        0x0303},
    {REG_FRAME_CYCLE_CONTROL_1,         0x000B},
    {REG_FRAME_CYCLE_CONTROL,           0x0003},
    {REG_EXTERNAL_INTERFACE_CONTROL,    0x0000},
    {REG_VERTICAL_SCROLL_CONTROL,       0x0000},
    {REG_MDDI_WAKEUP_CONTROL,           0x0000},
    {REG_MTP_INIT,                      0x0005},
    {REG_GOE_SIGNAL_TIMING,             0x000B},
    {REG_GATE_START_PULSE_DELAY_TIMING, 0x0000},
    {REG_VCOM_OUTPUT_CONTROL,           0x0000},
    {REG_PANEL_SIGNAL_CONTROL_2,        0x0000},
    {REG_PANEL_SIGNAL_CONTROL_1,        0x0007},
    {REG_DISPLAY_CONTROL,               0x0051},
    {DELAY_MS, 1},
    {REG_DISPLAY_CONTROL,               0x0053},
    {REG_PANEL_SIGNAL_CONTROL_1,        0x0000}
};
/*----------------------------------------------------------------------------*/
static struct DisplayPoint addressToPoint(uint32_t address)
{
  const uint16_t y = address / DISPLAY_WIDTH;
  const uint16_t x = address % DISPLAY_WIDTH;

  return (struct DisplayPoint){x, y};
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object __attribute__((unused)))
{
//  struct S6D1121 *display = object;
}
/*----------------------------------------------------------------------------*/
static void setPosition(struct S6D1121 *display, uint16_t x, uint16_t y)
{
  writeRegister(display, REG_GRAM_ADDRESS_X, x);
  writeRegister(display, REG_GRAM_ADDRESS_Y, y);
}
/*----------------------------------------------------------------------------*/
static void setWindow(struct S6D1121 *display, uint16_t beginX,
    uint16_t beginY, uint16_t endX, uint16_t endY)
{
  writeRegister(display, REG_HORIZONTAL_WINDOW_ADDRESS, beginX | (endX << 8));
  writeRegister(display, REG_VERTICAL_WINDOW_ADDRESS_END, endY);
  writeRegister(display, REG_VERTICAL_WINDOW_ADDRESS_BEGIN, beginY);
}
/*----------------------------------------------------------------------------*/
static inline void writeAddress(struct S6D1121 *display,
    enum displayRegister address)
{
  const uint8_t buffer[2] = {(uint8_t)(address >> 8), (uint8_t)address};

  pinReset(display->rs);
  ifWrite(display->bus, buffer, sizeof(buffer));
}
/*----------------------------------------------------------------------------*/
static inline void writeData(struct S6D1121 *display, uint16_t data)
{
  const uint8_t buffer[2] = {(uint8_t)(data >> 8), (uint8_t)data};

  pinSet(display->rs);
  ifWrite(display->bus, buffer, sizeof(buffer));
}
/*----------------------------------------------------------------------------*/
static void writeRegister(struct S6D1121 *display, enum displayRegister address,
    uint16_t data)
{
  writeAddress(display, address);
  writeData(display, data);
}
/*----------------------------------------------------------------------------*/
static enum result displayInit(void *object, const void *configPtr)
{
  const struct S6D1121Config * const config = configPtr;
  struct S6D1121 *display = object;
//  enum result res;

  assert(config->bus);

//  if ((res = ifSet(config->bus, IF_ZEROCOPY, 0)) != E_OK)
//    return res;
//  if ((res = ifCallback(config->bus, interruptHandler, display)) != E_OK)
//    return res;

  display->cs = pinInit(config->cs);
  if (!pinValid(display->cs))
    return E_VALUE;
  pinOutput(display->cs, 1);

  display->rs = pinInit(config->rs);
  if (!pinValid(display->rs))
    return E_VALUE;
  pinOutput(display->rs, 0);

  display->callback = 0;
  display->bus = config->bus;
  display->window.x = 0;
  display->window.y = 0;

  pinReset(display->cs);

  for (unsigned int index = 0; index < ARRAY_SIZE(initSequence); ++index)
  {
    if (initSequence[index].address != DELAY_MS)
    {
      writeRegister(display, initSequence[index].address,
          initSequence[index].value);
    }
    else
    {
      mdelay(initSequence[index].value);
    }
  }

  //FIXME End copypaste
  pinSet(display->cs);

  //FIXME Rewrite
  pinReset(display->cs);
  setWindow(display, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
  setPosition(display, 0, 0);
  pinSet(display->cs);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void displayDeinit(void *object __attribute__((unused)))
{

}
/*----------------------------------------------------------------------------*/
static enum result displayCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct S6D1121 *display = object;

  display->callbackArgument = argument;
  display->callback = callback;
  return E_OK;
}
/*----------------------------------------------------------------------------*/
static enum result displayGet(void *object, enum ifOption option, void *data)
{
  struct S6D1121 *display = object;

  switch ((enum ifDisplayOption)option)
  {
    case IF_DISPLAY_WIDTH:
      *(uint32_t *)data = DISPLAY_WIDTH;
      return E_OK;

    case IF_DISPLAY_HEIGHT:
      *(uint32_t *)data = DISPLAY_HEIGHT;
      return E_OK;

    default:
      break;
  }

  switch (option)
  {
    case IF_STATUS:
      return ifGet(display->bus, IF_STATUS, 0);

    default:
      return E_ERROR;
  }
}
/*----------------------------------------------------------------------------*/
static enum result displaySet(void *object, enum ifOption option,
    const void *data)
{
  struct S6D1121 *display = object;

  switch ((enum ifDisplayOption)option)
  {
    case IF_DISPLAY_WINDOW_BEGIN:
    {
      const struct DisplayPoint point = addressToPoint(*(const uint32_t *)data);

      if (point.y >= DISPLAY_HEIGHT)
        return E_VALUE;

      display->window.x = point.x;
      display->window.y = point.y;
      return E_OK;
    }

    case IF_DISPLAY_WINDOW_END:
    {
      const struct DisplayPoint point = addressToPoint(*(const uint32_t *)data);

      if (point.y >= DISPLAY_HEIGHT)
        return E_VALUE;

      pinReset(display->cs);
      setWindow(display, display->window.x, display->window.y,
          point.x, point.y);
      pinSet(display->cs);

      return E_OK;
    }

    default:
      break;
  }

  switch (option)
  {
    case IF_POSITION:
    {
      const struct DisplayPoint point = addressToPoint(*(const uint32_t *)data);

      if (point.y >= DISPLAY_HEIGHT)
        return E_VALUE;

      pinReset(display->cs);
      setPosition(display, point.x, point.y);
      pinSet(display->cs);

      return E_OK;
    }

    default:
      return E_ERROR;
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
  struct S6D1121 *display = object;

  pinReset(display->cs);
  writeAddress(display, REG_GRAM_DATA);
  pinSet(display->rs);
  ifWrite(display->bus, buffer, length);
  pinSet(display->cs);

  return length;
}
