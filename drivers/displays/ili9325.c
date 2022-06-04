/*
 * ili9325.c
 * Copyright (C) 2021 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/displays/display.h>
#include <dpm/displays/ili9325.h>
#include <halm/delay.h>
#include <xcore/bits.h>
#include <xcore/memory.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
#define DISPLAY_HEIGHT  320
#define DISPLAY_WIDTH   240

#define ENTRY_MODE_AM   BIT(3)
#define ENTRY_MODE_ID0  BIT(4)
#define ENTRY_MODE_ID1  BIT(5)
#define ENTRY_MODE_ORG  BIT(7)
#define ENTRY_MODE_BGR  BIT(12)
#define ENTRY_MODE_DFM  BIT(14)
#define ENTRY_MODE_TRI  BIT(15)
/*----------------------------------------------------------------------------*/
enum DisplayRegister
{
  REG_DRIVER_CODE_READ                  = 0x00,
  REG_DRIVER_OUTPUT_CONTROL_1           = 0x01,
  REG_LCD_DRIVING_CONTROL               = 0x02,
  REG_ENTRY_MODE                        = 0x03,
  REG_RESIZE_CONTROL                    = 0x04,
  REG_DISPLAY_CONTROL_1                 = 0x07,
  REG_DISPLAY_CONTROL_2                 = 0x08,
  REG_DISPLAY_CONTROL_3                 = 0x09,
  REG_DISPLAY_CONTROL_4                 = 0x0A,
  REG_RGB_DISPLAY_INTERFACE_CONTROL_1   = 0x0C,
  REG_FRAME_MARKER_POSITION             = 0x0D,
  REG_RGB_DISPLAY_INTERFACE_CONTROL_2   = 0x0F,
  REG_POWER_CONTROL_1                   = 0x10,
  REG_POWER_CONTROL_2                   = 0x11,
  REG_POWER_CONTROL_3                   = 0x12,
  REG_POWER_CONTROL_4                   = 0x13,
  REG_HORIZONTAL_GRAM_ADDRESS_SET       = 0x20,
  REG_VERTICAL_GRAM_ADDRESS_SET         = 0x21,
  REG_WRITE_DATA_TO_GRAM                = 0x22,
  REG_POWER_CONTROL_7                   = 0x29,
  REG_FRAME_RATE_AND_COLOR_CONTROL      = 0x2B,
  REG_GAMMA_CONTROL_1                   = 0x30,
  REG_GAMMA_CONTROL_2                   = 0x31,
  REG_GAMMA_CONTROL_3                   = 0x32,
  REG_GAMMA_CONTROL_4                   = 0x35,
  REG_GAMMA_CONTROL_5                   = 0x36,
  REG_GAMMA_CONTROL_6                   = 0x37,
  REG_GAMMA_CONTROL_7                   = 0x38,
  REG_GAMMA_CONTROL_8                   = 0x39,
  REG_GAMMA_CONTROL_9                   = 0x3C,
  REG_GAMMA_CONTROL_10                  = 0x3D,
  REG_HORIZONTAL_ADDRESS_START          = 0x50,
  REG_HORIZONTAL_ADDRESS_END            = 0x51,
  REG_VERTICAL_ADDRESS_START            = 0x52,
  REG_VERTICAL_ADDRESS_END              = 0x53,
  REG_DRIVER_OUTPUT_CONTROL_2           = 0x60,
  REG_BASE_IMAGE_DISPLAY_CONTROL        = 0x61,
  REG_VERTICAL_SCROLL_CONTROL           = 0x6A,
  REG_PARTIAL_IMAGE_1_DISPLAY_POSITION  = 0x80,
  REG_PARTIAL_IMAGE_1_AREA_START        = 0x81,
  REG_PARTIAL_IMAGE_1_AREA_END          = 0x82,
  REG_PARTIAL_IMAGE_2_DISPLAY_POSITION  = 0x83,
  REG_PARTIAL_IMAGE_2_AREA_START        = 0x84,
  REG_PARTIAL_IMAGE_2_AREA_END          = 0x85,
  REG_PANEL_INTERFACE_CONTROL_1         = 0x90,
  REG_PANEL_INTERFACE_CONTROL_2         = 0x92,
  REG_RESERVED_0                        = 0x93,
  REG_PANEL_INTERFACE_CONTROL_4         = 0x95,
  REG_RESERVED_1                        = 0x97,
  REG_RESERVED_2                        = 0x98,
  REG_OTP_VCM_PROGRAMMING_CONTROL       = 0xA1,
  REG_OTP_VCM_STATUS_AND_ENABLE         = 0xA2,
  REG_OTP_PROGRAMMING_ID_KEY            = 0xA5,

  /* Service definitions */
  DELAY_MS                              = 0xFF
};
/*----------------------------------------------------------------------------*/
struct InitEntry
{
  uint16_t address;
  uint16_t value;
};
/*----------------------------------------------------------------------------*/
static void deselectChip(struct ILI9325 *);
static void selectChip(struct ILI9325 *);
static void selectCommandMode(struct ILI9325 *);
static void selectDataMode(struct ILI9325 *);
static void setOrientation(struct ILI9325 *, enum DisplayOrientation);
static void setWindow(struct ILI9325 *, const struct DisplayWindow *);
static void writeAddress(struct ILI9325 *, enum DisplayRegister);
static void writeData(struct ILI9325 *, uint16_t);
static void writeRegister(struct ILI9325 *, enum DisplayRegister, uint16_t);
/*----------------------------------------------------------------------------*/
static enum Result displayInit(void *, const void *);
static void displayDeinit(void *);
static enum Result displayGetParam(void *, int, void *);
static enum Result displaySetParam(void *, int, const void *);
static size_t displayRead(void *, void *, size_t);
static size_t displayWrite(void *, const void *, size_t);
/*----------------------------------------------------------------------------*/
const struct InterfaceClass * const ILI9325 = &(const struct InterfaceClass){
    .size = sizeof(struct ILI9325),
    .init = displayInit,
    .deinit = displayDeinit,

    .setCallback = 0,
    .getParam = displayGetParam,
    .setParam = displaySetParam,
    .read = displayRead,
    .write = displayWrite
};
/*----------------------------------------------------------------------------*/
static const struct InitEntry initSequence[] = {
    /* Initial sequence */
    {REG_DRIVER_OUTPUT_CONTROL_1,          0x0100},
    {REG_LCD_DRIVING_CONTROL,              0x0700},
    {REG_RESIZE_CONTROL,                   0x0000},
    {REG_DISPLAY_CONTROL_2,                0x0202},
    {REG_DISPLAY_CONTROL_3,                0x0000},
    {REG_DISPLAY_CONTROL_4,                0x0000},
    {REG_RGB_DISPLAY_INTERFACE_CONTROL_1,  0x0000},
    {REG_FRAME_MARKER_POSITION,            0x0000},
    {REG_RGB_DISPLAY_INTERFACE_CONTROL_2,  0x0000},

    /* Power On sequence */
    {REG_POWER_CONTROL_1,                  0x0000},
    {REG_POWER_CONTROL_2,                  0x0000},
    {REG_POWER_CONTROL_3,                  0x0000},
    {REG_POWER_CONTROL_4,                  0x0000},

    {REG_POWER_CONTROL_1,                  0x17B0},
    {REG_POWER_CONTROL_2,                  0x0137},
    {REG_POWER_CONTROL_3,                  0x0139},
    {REG_POWER_CONTROL_4,                  0x1D00},
    {REG_POWER_CONTROL_7,                  0x0013},

    /* Adjust Gamma Curve */
    {REG_GAMMA_CONTROL_1,                  0x0007},
    {REG_GAMMA_CONTROL_2,                  0x0302},
    {REG_GAMMA_CONTROL_3,                  0x0105},
    {REG_GAMMA_CONTROL_4,                  0x0206},
    {REG_GAMMA_CONTROL_5,                  0x0808},
    {REG_GAMMA_CONTROL_6,                  0x0206},
    {REG_GAMMA_CONTROL_7,                  0x0504},
    {REG_GAMMA_CONTROL_8,                  0x0007},
    {REG_GAMMA_CONTROL_9,                  0x0105},
    {REG_GAMMA_CONTROL_10,                 0x0808},

    /* Configure GRAM area */
    {REG_DRIVER_OUTPUT_CONTROL_2,          0xA700},
    {REG_BASE_IMAGE_DISPLAY_CONTROL,       0x0001},
    {REG_VERTICAL_SCROLL_CONTROL,          0x0000},

    /* Partial Image Control */
    {REG_PARTIAL_IMAGE_1_DISPLAY_POSITION, 0x0000},
    {REG_PARTIAL_IMAGE_1_AREA_START,       0x0000},
    {REG_PARTIAL_IMAGE_1_AREA_END,         0x0000},
    {REG_PARTIAL_IMAGE_2_DISPLAY_POSITION, 0x0000},
    {REG_PARTIAL_IMAGE_2_AREA_START,       0x0000},
    {REG_PARTIAL_IMAGE_2_AREA_END,         0x0000},

    /* Panel Control */
    {REG_PANEL_INTERFACE_CONTROL_1,        0x0010},
    {REG_PANEL_INTERFACE_CONTROL_2,        0x0000},
    {REG_RESERVED_0,                       0x0003},
    {REG_PANEL_INTERFACE_CONTROL_4,        0x0110},
    {REG_RESERVED_1,                       0x0000},
    {REG_RESERVED_2,                       0x0000},

    /* Display enable */
    {REG_DISPLAY_CONTROL_1,                0x0173}
};
/*----------------------------------------------------------------------------*/
static void deselectChip(struct ILI9325 *display)
{
  pinSet(display->cs);
}
/*----------------------------------------------------------------------------*/
static void selectChip(struct ILI9325 *display)
{
  pinReset(display->cs);
}
/*----------------------------------------------------------------------------*/
static void selectCommandMode(struct ILI9325 *display)
{
  pinReset(display->rs);
}
/*----------------------------------------------------------------------------*/
static void selectDataMode(struct ILI9325 *display)
{
  pinSet(display->rs);
}
/*----------------------------------------------------------------------------*/
static void setOrientation(struct ILI9325 *display,
    enum DisplayOrientation orientation)
{
  uint16_t value = ENTRY_MODE_BGR;

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
static void setWindow(struct ILI9325 *display,
    const struct DisplayWindow *window)
{
  selectChip(display);

  writeRegister(display, REG_HORIZONTAL_ADDRESS_START, window->ax);
  writeRegister(display, REG_HORIZONTAL_ADDRESS_END, window->bx);

  writeRegister(display, REG_VERTICAL_ADDRESS_START, window->ay);
  writeRegister(display, REG_VERTICAL_ADDRESS_END, window->by);

  writeRegister(display, REG_HORIZONTAL_GRAM_ADDRESS_SET, window->ax);
  writeRegister(display, REG_VERTICAL_GRAM_ADDRESS_SET, window->ay);

  deselectChip(display);
}
/*----------------------------------------------------------------------------*/
static void writeAddress(struct ILI9325 *display,
    enum DisplayRegister address)
{
  const uint16_t buffer = toBigEndian16((uint16_t)address);

  selectCommandMode(display);
  ifWrite(display->bus, &buffer, sizeof(buffer));
}
/*----------------------------------------------------------------------------*/
static void writeData(struct ILI9325 *display, uint16_t data)
{
  const uint16_t buffer = toBigEndian16(data);

  selectDataMode(display);
  ifWrite(display->bus, &buffer, sizeof(buffer));
}
/*----------------------------------------------------------------------------*/
static void writeRegister(struct ILI9325 *display, enum DisplayRegister address,
    uint16_t data)
{
  writeAddress(display, address);
  writeData(display, data);
}
/*----------------------------------------------------------------------------*/
static enum Result displayInit(void *object, const void *configPtr)
{
  const struct ILI9325Config * const config = configPtr;
  assert(config);
  assert(config->bus);

  struct ILI9325 * const display = object;

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

  display->bus = config->bus;

  /* Reset display */
  pinReset(display->reset);
  mdelay(20);
  pinSet(display->reset);
  mdelay(20);

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
static enum Result displayGetParam(void *object, int parameter, void *data)
{
  struct ILI9325 * const display = object;

  switch ((enum IfDisplayParameter)parameter)
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
      return ifGetParam(display->bus, IF_STATUS, 0);

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result displaySetParam(void *object, int parameter,
    const void *data)
{
  struct ILI9325 * const display = object;

  switch ((enum IfDisplayParameter)parameter)
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
  struct ILI9325 * const display = object;
  size_t bytesRead;

  selectChip(display);
  writeAddress(display, REG_WRITE_DATA_TO_GRAM);

  selectDataMode(display);
  bytesRead = ifRead(display->bus, buffer, length);
  deselectChip(display);

  return bytesRead;
}
/*----------------------------------------------------------------------------*/
static size_t displayWrite(void *object, const void *buffer, size_t length)
{
  struct ILI9325 * const display = object;
  size_t bytesWritten;

  selectChip(display);
  writeAddress(display, REG_WRITE_DATA_TO_GRAM);

  selectDataMode(display);
  bytesWritten = ifWrite(display->bus, buffer, length);
  deselectChip(display);

  return bytesWritten;
}
