/*
 * s6d1121.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/displays/display.h>
#include <dpm/displays/s6d1121.h>
#include <halm/delay.h>
#include <xcore/bits.h>
#include <xcore/memory.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
#define DISPLAY_HEIGHT  320
#define DISPLAY_WIDTH   240

#define ENTRY_MODE_ID0  BIT(0)
#define ENTRY_MODE_ID1  BIT(1)
#define ENTRY_MODE_AM   BIT(3)
#define ENTRY_MODE_BGR  BIT(12)
#define ENTRY_MODE_DFM  BIT(14)
#define ENTRY_MODE_TRI  BIT(15)
/*----------------------------------------------------------------------------*/
enum DisplayRegister
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

  /* Service definitions */
  DELAY_MS                            = 0xFF
};
/*----------------------------------------------------------------------------*/
struct InitEntry
{
  uint16_t address;
  uint16_t value;
};
/*----------------------------------------------------------------------------*/
static void deselectChip(struct S6D1121 *);
static void selectChip(struct S6D1121 *);
static void selectCommandMode(struct S6D1121 *);
static void selectDataMode(struct S6D1121 *);
static void interruptHandler(void *);
static void setOrientation(struct S6D1121 *, enum DisplayOrientation);
static void setWindow(struct S6D1121 *, const struct DisplayWindow *);
static void writeAddress(struct S6D1121 *, enum DisplayRegister);
static void writeData(struct S6D1121 *, uint16_t);
static void writeRegister(struct S6D1121 *, enum DisplayRegister, uint16_t);
/*----------------------------------------------------------------------------*/
static enum Result displayInit(void *, const void *);
static void displayDeinit(void *);
static void displaySetCallback(void *, void (*)(void *), void *);
static enum Result displayGetParam(void *, int, void *);
static enum Result displaySetParam(void *, int, const void *);
static size_t displayWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const S6D1121 = &(const struct InterfaceClass){
    .size = sizeof(struct S6D1121),
    .init = displayInit,
    .deinit = displayDeinit,

    .setCallback = displaySetCallback,
    .getParam = displayGetParam,
    .setParam = displaySetParam,
    .read = NULL,
    .write = displayWrite
};
/*----------------------------------------------------------------------------*/
static const struct InitEntry initSequence[] = {
    {REG_POWER_CONTROL_2,               0x2004},
    {REG_POWER_CONTROL_4,               0xCC00},
    {REG_POWER_CONTROL_6,               0x2600},
    {REG_POWER_CONTROL_5,               0x252A},
    {REG_POWER_CONTROL_3,               0x0033},
    {REG_POWER_CONTROL_4,               0xCC04},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4,               0xCC06},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4,               0xCC4F},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4,               0x674F},
    {REG_POWER_CONTROL_2,               0x2003},
    {DELAY_MS, 1},

    /* Gamma settings */
    {REG_GAMMA_CONTROL_1,               0x2609},
    {REG_GAMMA_CONTROL_2,               0x242C},
    {REG_GAMMA_CONTROL_3,               0x1F23},
    {REG_GAMMA_CONTROL_4,               0x2425},
    {REG_GAMMA_CONTROL_5,               0x2226},
    {REG_GAMMA_CONTROL_6,               0x2523},
    {REG_GAMMA_CONTROL_7,               0x1C1A},
    {REG_GAMMA_CONTROL_8,               0x131D},
    {REG_GAMMA_CONTROL_9,               0x0B11},
    {REG_GAMMA_CONTROL_10,              0x1210},
    {REG_GAMMA_CONTROL_11,              0x1315},
    {REG_GAMMA_CONTROL_12,              0x3619},
    {REG_GAMMA_CONTROL_13,              0x0D00},
    {REG_GAMMA_CONTROL_14,              0x000D},

    {REG_POWER_CONTROL_7,               0x0007},
    {REG_LCD_DRIVING_WAVEFORM_CONTROL,  0x0013},
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
static void deselectChip(struct S6D1121 *display)
{
  pinSet(display->cs);
}
/*----------------------------------------------------------------------------*/
static void selectChip(struct S6D1121 *display)
{
  pinReset(display->cs);
}
/*----------------------------------------------------------------------------*/
static void selectCommandMode(struct S6D1121 *display)
{
  pinReset(display->rs);
}
/*----------------------------------------------------------------------------*/
static void selectDataMode(struct S6D1121 *display)
{
  pinSet(display->rs);
}
/*----------------------------------------------------------------------------*/
static void interruptHandler(void *object)
{
  struct S6D1121 * const display = object;

  /* Release Chip Select */
  deselectChip(display);
  /* Restore blocking mode */
  ifSetCallback(display->bus, NULL, NULL);
  ifSetParam(display->bus, IF_BLOCKING, NULL);

  if (display->callback)
    display->callback(display->callbackArgument);
}
/*----------------------------------------------------------------------------*/
static void setOrientation(struct S6D1121 *display,
    enum DisplayOrientation orientation)
{
  uint16_t value = 0;

  switch (orientation)
  {
    case DISPLAY_ORIENTATION_NORMAL:
      value |= ENTRY_MODE_ID1 | ENTRY_MODE_ID0;
      break;

    case DISPLAY_ORIENTATION_MIRROR_X:
      value |= ENTRY_MODE_ID0;
      break;

    case DISPLAY_ORIENTATION_MIRROR_Y:
      value |= ENTRY_MODE_ID1;
      break;

    default:
      break;
  }

  selectChip(display);
  writeRegister(display, REG_ENTRY_MODE, value);
  deselectChip(display);
}
/*----------------------------------------------------------------------------*/
static void setWindow(struct S6D1121 *display,
    const struct DisplayWindow *window)
{
  selectChip(display);

  writeRegister(display, REG_HORIZONTAL_WINDOW_ADDRESS,
      window->ax | (window->bx << 8));

  writeRegister(display, REG_VERTICAL_WINDOW_ADDRESS_END, window->by);
  writeRegister(display, REG_VERTICAL_WINDOW_ADDRESS_BEGIN, window->ay);

  writeRegister(display, REG_GRAM_ADDRESS_X, window->ax);
  writeRegister(display, REG_GRAM_ADDRESS_Y, window->ay);

  deselectChip(display);
}
/*----------------------------------------------------------------------------*/
static void writeAddress(struct S6D1121 *display,
    enum DisplayRegister address)
{
  const uint16_t buffer = toBigEndian16((uint16_t)address);

  selectCommandMode(display);
  ifWrite(display->bus, &buffer, sizeof(buffer));
}
/*----------------------------------------------------------------------------*/
static void writeData(struct S6D1121 *display, uint16_t data)
{
  const uint16_t buffer = toBigEndian16(data);

  selectDataMode(display);
  ifWrite(display->bus, &buffer, sizeof(buffer));
}
/*----------------------------------------------------------------------------*/
static void writeRegister(struct S6D1121 *display, enum DisplayRegister address,
    uint16_t data)
{
  writeAddress(display, address);
  writeData(display, data);
}
/*----------------------------------------------------------------------------*/
static enum Result displayInit(void *object, const void *configPtr)
{
  const struct S6D1121Config * const config = configPtr;
  assert(config != NULL);
  assert(config->bus != NULL);

  struct S6D1121 * const display = object;

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

  display->callback = NULL;
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

  selectChip(display);
  for (size_t index = 0; index < ARRAY_SIZE(initSequence); ++index)
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
  struct S6D1121 * const display = object;

  display->callbackArgument = argument;
  display->callback = callback;
}
/*----------------------------------------------------------------------------*/
static enum Result displayGetParam(void *object, int parameter, void *data)
{
  struct S6D1121 * const display = object;

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
  struct S6D1121 * const display = object;

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
static size_t displayWrite(void *object, const void *buffer, size_t length)
{
  struct S6D1121 * const display = object;
  size_t bytesWritten;

  selectChip(display);
  writeAddress(display, REG_GRAM_DATA);

  selectDataMode(display);

  if (display->blocking)
  {
    bytesWritten = ifWrite(display->bus, buffer, length);
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
      deselectChip(display);
      ifSetCallback(display->bus, NULL, NULL);
      ifSetParam(display->bus, IF_BLOCKING, NULL);
    }
  }

  return bytesWritten;
}
