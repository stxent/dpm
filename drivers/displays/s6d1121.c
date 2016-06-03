/*
 * s6d1121.c
 * Copyright (C) 2014 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <halm/delay.h>
#include <xcore/bits.h>
#include <dpm/drivers/displays/display.h>
#include <dpm/drivers/displays/s6d1121.h>
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
static void setOrientation(struct S6D1121 *, enum displayOrientation);
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
const struct InterfaceClass * const S6D1121 = &displayTable;
/*----------------------------------------------------------------------------*/
static const struct InitEntry initSequence[] = {
    // TODO Delays
    {REG_POWER_CONTROL_2, 0x2004},
    {REG_POWER_CONTROL_4, 0xCC00},
    {REG_POWER_CONTROL_6, 0x2600},
    {REG_POWER_CONTROL_5, 0x252A},
    {REG_POWER_CONTROL_3, 0x0033},
    {REG_POWER_CONTROL_4, 0xCC04},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4, 0xCC06},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4, 0xCC4F},
    {DELAY_MS, 1},
    {REG_POWER_CONTROL_4, 0x674F},
    {REG_POWER_CONTROL_2, 0x2003},
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

    {REG_POWER_CONTROL_7,               0x0007},
    {REG_LCD_DRIVING_WAVEFORM_CONTROL,  0x0013},
    {REG_ENTRY_MODE,                    0x0000},
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
  if (!display->csExternal)
    pinSet(display->cs);
}
/*----------------------------------------------------------------------------*/
static void selectChip(struct S6D1121 *display)
{
  if (!display->csExternal)
    pinReset(display->cs);
}
/*----------------------------------------------------------------------------*/
static void setOrientation(struct S6D1121 *display,
    enum displayOrientation orientation)
{
  selectChip(display);
  writeRegister(display, REG_ENTRY_MODE, (uint16_t)orientation);
  deselectChip(display);
}
/*----------------------------------------------------------------------------*/
static void setWindow(struct S6D1121 *display, uint16_t x0, uint16_t y0,
    uint16_t x1, uint16_t y1)
{
  selectChip(display);
  writeRegister(display, REG_HORIZONTAL_WINDOW_ADDRESS, x0 | (x1 << 8));
  writeRegister(display, REG_VERTICAL_WINDOW_ADDRESS_END, y1);
  writeRegister(display, REG_VERTICAL_WINDOW_ADDRESS_BEGIN, y0);

  writeRegister(display, REG_GRAM_ADDRESS_X, x0);
  writeRegister(display, REG_GRAM_ADDRESS_Y, y0);
  deselectChip(display);
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
  struct S6D1121 * const display = object;

  assert(config->bus);

  display->reset = pinInit(config->reset);
  if (!pinValid(display->reset))
    return E_VALUE;
  pinOutput(display->reset, 1);

  if (config->cs)
  {
    display->cs = pinInit(config->cs);
    if (!pinValid(display->cs))
      return E_VALUE;
    pinOutput(display->cs, 1);

    display->csExternal = false;
  }
  else
    display->csExternal = true;

  display->rs = pinInit(config->rs);
  if (!pinValid(display->rs))
    return E_VALUE;
  pinOutput(display->rs, 0);

  display->bus = config->bus;

  /* Reset display */
  pinReset(display->reset);
  mdelay(20);
  pinSet(display->reset);
  mdelay(20);

  selectChip(display);
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
  deselectChip(display);

  setWindow(display, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void displayDeinit(void *object __attribute__((unused)))
{

}
/*----------------------------------------------------------------------------*/
static enum result displayCallback(void *object __attribute__((unused)),
    void (*callback)(void *) __attribute__((unused)),
    void *argument __attribute__((unused)))
{
  return E_ERROR;
}
/*----------------------------------------------------------------------------*/
static enum result displayGet(void *object, enum ifOption option, void *data)
{
  struct S6D1121 * const display = object;

  switch ((enum ifDisplayOption)option)
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
  struct S6D1121 * const display = object;

  switch ((enum ifDisplayOption)option)
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
  struct S6D1121 * const display = object;
  size_t bytesRead;

  selectChip(display);
  writeAddress(display, REG_GRAM_DATA);

  pinSet(display->rs);
  bytesRead = ifRead(display->bus, buffer, length);
  deselectChip(display);

  return bytesRead;
}
/*----------------------------------------------------------------------------*/
static size_t displayWrite(void *object, const void *buffer, size_t length)
{
  struct S6D1121 * const display = object;
  size_t bytesWritten;

  selectChip(display);
  writeAddress(display, REG_GRAM_DATA);

  pinSet(display->rs);
  bytesWritten = ifWrite(display->bus, buffer, length);
  deselectChip(display);

  return bytesWritten;
}
