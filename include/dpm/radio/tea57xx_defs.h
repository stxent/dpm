/*
 * sensors/tea57xx_defs.h
 * Copyright (C) 2025 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_SENSORS_TEA57XX_DEFS_H_
#define DPM_SENSORS_TEA57XX_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <xcore/bits.h>
/*----------------------------------------------------------------------------*/
enum
{
  FLAG_RESET    = 0x01,
  FLAG_CONFIG   = 0x02,
  FLAG_STATUS   = 0x04,
  FLAG_SUSPEND  = 0x08,
  FLAG_SEARCH   = 0x10
};

#define FREQUENCY_INTERMEDIATE          225000
#define FREQUENCY_INITIAL               100000000
#define FREQUENCY_XTAL_HS               50000 /* 13 MHz or 6.5 MHz */
#define FREQUENCY_XTAL_LS               32768 /* 32768 Hz */
/*------------------Read Data Byte 1------------------------------------------*/
#define RDB1_PLL(value)                 BIT_FIELD((value), 0)
#define RDB1_PLL_MASK                   BIT_FIELD(MASK(6), 0)
#define RDB1_PLL_VALUE(reg)             FIELD_VALUE((reg), RDB1_PLL_MASK, 0)

#define RDB1_BLF                        BIT(6) /* Band Limit Flag */
#define RDB1_RF                         BIT(7) /* Ready Flag */
/*------------------Read Data Byte 2------------------------------------------*/
#define RDB2_PLL(value)                 BIT_FIELD((value), 0)
#define RDB2_PLL_MASK                   BIT_FIELD(MASK(8), 0)
#define RDB2_PLL_VALUE(reg)             FIELD_VALUE((reg), RDB2_PLL_MASK, 0)
/*------------------Read Data Byte 3------------------------------------------*/
#define RDB3_IF(value)                  BIT_FIELD((value), 0)
#define RDB3_IF_MASK                    BIT_FIELD(MASK(7), 0)
#define RDB3_IF_VALUE(reg)              FIELD_VALUE((reg), RDB3_IF_MASK, 0)

#define RDB3_STEREO                     BIT(7) /* Stereo Indication */
/*------------------Read Data Byte 4------------------------------------------*/
#define RDB4_CI(value)                  BIT_FIELD((value), 1)
#define RDB4_CI_MASK                    BIT_FIELD(MASK(3), 1)
#define RDB4_CI_VALUE(reg)              FIELD_VALUE((reg), RDB4_CI_MASK, 1)

#define RDB4_LEV(value)                 BIT_FIELD((value), 4)
#define RDB4_LEV_MASK                   BIT_FIELD(MASK(4), 4)
#define RDB4_LEV_VALUE(reg)             FIELD_VALUE((reg), RDB4_LEV_MASK, 4)
/*------------------Write Data Byte 1-----------------------------------------*/
#define WDB1_PLL(value)                 BIT_FIELD((value), 0)
#define WDB1_PLL_MASK                   BIT_FIELD(MASK(6), 0)
#define WDB1_PLL_VALUE(reg)             FIELD_VALUE((reg), WDB1_PLL_MASK, 0)

#define WDB1_SM                         BIT(6) /* Search Mode */
#define WDB1_MUTE                       BIT(7) /* Left and Right Mute */
/*------------------Write Data Byte 2-----------------------------------------*/
#define WDB2_PLL(value)                 BIT_FIELD((value), 0)
#define WDB2_PLL_MASK                   BIT_FIELD(MASK(8), 0)
#define WDB2_PLL_VALUE(reg)             FIELD_VALUE((reg), WDB2_PLL_MASK, 0)
/*------------------Write Data Byte 3-----------------------------------------*/
#define WDB3_SWP1                       BIT(0) /* Programmable port 1 */
#define WDB3_ML                         BIT(1) /* Mute Left */
#define WDB3_MR                         BIT(2) /* Mute Right */
#define WDB3_MS                         BIT(3) /* Mono to Stereo */
#define WDB3_HLSI                       BIT(4) /* High/Low Side Injection */

/* Search Stop Level */
#define WDB3_SSL(value)                 BIT_FIELD((value), 5)
#define WDB3_SSL_MASK                   BIT_FIELD(MASK(2), 5)
#define WDB3_SSL_VALUE(reg)             FIELD_VALUE((reg), WDB3_SSL_MASK, 5)

#define WDB3_SUD                        BIT(7) /* Search Up/Down */
/*------------------Write Data Byte 4-----------------------------------------*/
#define WDB4_SI                         BIT(0) /* Search Indicator */
#define WDB4_SNC                        BIT(1) /* Stereo Noise Cancelling */
#define WDB4_HCC                        BIT(2) /* High Cut Control */
#define WDB4_SMUTE                      BIT(3) /* Soft Mute */
#define WDB4_XTAL                       BIT(4) /* Enable 32.768kHz XTAL */
#define WDB4_BL                         BIT(5) /* Band Limits */
#define WDB4_STBY                       BIT(6) /* Enable standby mode */
#define WDB4_SWP2                       BIT(7) /* Programmable port 2 */
/*------------------Write Data Byte 5-----------------------------------------*/
#define WDB5_DTC                        BIT(6) /* De-emphasis control */
#define WDB5_PLLREF                     BIT(7) /* Enable 6.5MHz PLL reference */
/*----------------------------------------------------------------------------*/
#endif /* DPM_SENSORS_TEA57XX_DEFS_H_ */
