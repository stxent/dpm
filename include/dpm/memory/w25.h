/*
 * memory/w25.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_MEMORY_W25_H_
#define DPM_MEMORY_W25_H_
/*----------------------------------------------------------------------------*/
enum [[gnu::packed]] W25DriverStrength
{
  W25_DRV_DEFAULT,
  W25_DRV_100PCT,
  W25_DRV_75PCT,
  W25_DRV_50PCT,
  W25_DRV_25PCT,

  W25_DRV_END
};
/*----------------------------------------------------------------------------*/
#endif /* DPM_MEMORY_W25_H_ */
