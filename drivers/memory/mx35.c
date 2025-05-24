/*
 * mx35.c
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/memory/flash_defs.h>
#include <dpm/memory/mx35.h>
/*----------------------------------------------------------------------------*/
struct MX35Info mx35GetDeviceInfo(uint8_t manufacturer, uint8_t device)
{
  /* Returns structure: blocks, ECC, QIO, 4K pages */

  if (manufacturer == JEDEC_MANUFACTURER_MACRONIX)
  {
    switch (device)
    {
      case 0x12:
        /* MX35LF1GE4AB */
        return (struct MX35Info){1024, true, false, false};
      case 0x14:
        /* MX35LF1G24AD */
        return (struct MX35Info){1024, false, true, false};
      case 0x20:
        /* MX35LF2G14AC */
        return (struct MX35Info){2048, false, true, false};
      case 0x22:
        /* MX35LF2GE4AB */
        return (struct MX35Info){2048, true, false, false};
      case 0x24:
      case 0x64:
        /* MX35LF2G24AD */
        return (struct MX35Info){2048, false, true, false};
      case 0x26:
        /* MX35LF2GE4AD */
        return (struct MX35Info){2048, true, true, false};
      case 0x35:
      case 0x75:
        /* MX35LF4G24AD */
        return (struct MX35Info){2048, false, true, true};
      case 0x37:
        /* MX35LF4GE4AD */
        return (struct MX35Info){2048, true, true, true};
      default:
        break;
    }
  }

  /* Unknown device */
  return (struct MX35Info){0, false, false, false};
}
