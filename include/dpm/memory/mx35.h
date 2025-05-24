/*
 * memory/mx35.h
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_MX35_H_
#define DPM_MEMORY_MX35_H_
/*----------------------------------------------------------------------------*/
#include <xcore/helpers.h>
#include <stdbool.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
struct MX35Info
{
  uint16_t blocks;
  bool ecc;
  bool qio;
  bool wide;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

struct MX35Info mx35GetDeviceInfo(uint8_t, uint8_t);

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_MX35_H_ */
