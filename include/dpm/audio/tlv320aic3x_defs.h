/*
 * audio/tlv320aic3x_defs.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_AUDIO_TLV320AIC3X_DEFS_H_
#define DPM_AUDIO_TLV320AIC3X_DEFS_H_
/*----------------------------------------------------------------------------*/
#include <xcore/bits.h>
/*----------------------------------------------------------------------------*/
enum
{
  /* Page Select register */
  REG_PAGE_SELECT                     = 0,
  /* Software Reset register */
  REG_SOFTWARE_RESET                  = 1,
  /* Codec Sample Rate Select register */
  REG_SAMPLE_RATE_SELECT              = 2,
  /* PLL Programming register A */
  REG_PLL_A                           = 3,
  /* PLL Programming register B */
  REG_PLL_B                           = 4,
  /* PLL Programming register C */
  REG_PLL_C                           = 5,
  /* PLL Programming register D */
  REG_PLL_D                           = 6,
  /* Codec Data-Path Setup register */
  REG_CODEC_DATA_PATH_SETUP           = 7,
  /* Audio Serial Data Interface Control register A */
  REG_ASD_IF_CTRL_A                   = 8,
  /* Audio Serial Data Interface Control register B */
  REG_ASD_IF_CTRL_B                   = 9,
  /* Audio Serial Data Interface Control register C */
  REG_ASD_IF_CTRL_C                   = 10,
  /* Audio Codec Overflow Flag and PLL R programming register */
  REG_CODEC_OVERFLOW_PLL_R            = 11,
  /* Audio Codec Digital Filter Control register */
  REG_CODEC_DFILT_CTRL                = 12,
  /* Headset/Button Press Detection register */
  REG_HEADSET_DETECT_CTRL_A           = 13,
  REG_HEADSET_DETECT_CTRL_B           = 14,
  /* ADC PGA Gain Control registers */
  REG_LADC_GAIN_CTRL                  = 15,
  REG_RADC_GAIN_CTRL                  = 16,
  /*
   * MIC2/LINE2 Control registers on TLV320AIC3101/3104
   * MIC3/LINE3 Control registers on TLV320AIC3105
   */
  REG_MIC2LR_LINE2LR_TO_LADC_CTRL     = 17,
  REG_MIC2LR_LINE2LR_TO_RADC_CTRL     = 18,
  /* MIC1LP/LINE1LP Control registers */
  REG_MIC1LP_LINE1LP_TO_LADC_CTRL     = 19,
  REG_MIC1RP_LINE1RP_TO_LADC_CTRL     = 21,
  REG_MIC1RP_LINE1RP_TO_RADC_CTRL     = 22,
  REG_MIC1LP_LINE1LP_TO_RADC_CTRL     = 24,
  /* MIC2/LINE2 Control registers on TLV320AIC3105 */
  REG_LINE2L_TO_LADC_CTRL             = 20,
  REG_LINE2R_TO_RADC_CTRL             = 23,
  /* MICBIAS Control register */
  REG_MICBIAS_CTRL                    = 25,

  /* AGC Control registers A, B, C */
  REG_LAGC_CTRL_A                     = 26,
  REG_LAGC_CTRL_B                     = 27,
  REG_LAGC_CTRL_C                     = 28,
  REG_RAGC_CTRL_A                     = 29,
  REG_RAGC_CTRL_B                     = 30,
  REG_RAGC_CTRL_C                     = 31,

  /* AGC Gain registers */
  REG_LAGC_GAIN                       = 32,
  REG_RAGC_GAIN                       = 33,

  /* AGC Noise Gate Debounce registers */
  REG_LAGC_NOISE_DEBOUNCE             = 34,
  REG_RAGC_NOISE_DEBOUNCE             = 35,

  /* ADC Flag register */
  REG_ADC_FLAGS                       = 36,

  /* DAC Power and Output Driver Control register */
  REG_HPLCOM_CFG_DAC_PWR              = 37,
  /* High-Power Output Driver Control register */
  REG_HPRCOM_CFG                      = 38,
  /* High-Power Output Stage Control register */
  REG_HPOUT_SC                        = 40,
  /* DAC Output Switching Control register */
  REG_DAC_MUX                         = 41,
  /* Output Driver Pop Reduction register */
  REG_HPOUT_POP_REDUCTION             = 42,
  /* DAC Digital Volume Control registers */
  REG_LDAC_VOL                        = 43,
  REG_RDAC_VOL                        = 44,

  /* Left High-Power Output Control registers */
  REG_PGAL_TO_HPLOUT_VOL              = 46,
  REG_DACL1_TO_HPLOUT_VOL             = 47,
  REG_PGAR_TO_HPLOUT_VOL              = 49,
  REG_DACR1_TO_HPLOUT_VOL             = 50,
  REG_HPLOUT_CTRL                     = 51,

  /* Left High-Power COM Control registers */
  REG_PGAL_TO_HPLCOM_VOL              = 53,
  REG_DACL1_TO_HPLCOM_VOL             = 54,
  REG_PGAR_TO_HPLCOM_VOL              = 56,
  REG_DACR1_TO_HPLCOM_VOL             = 57,
  REG_HPLCOM_CTRL                     = 58,

  /* Right High-Power Output Control registers */
  REG_PGAL_TO_HPROUT_VOL              = 60,
  REG_DACL1_TO_HPROUT_VOL             = 61,
  REG_PGAR_TO_HPROUT_VOL              = 63,
  REG_DACR1_TO_HPROUT_VOL             = 64,
  REG_HPROUT_CTRL                     = 65,

  /* Right High-Power COM Control registers */
  REG_PGAL_TO_HPRCOM_VOL              = 67,
  REG_DACL1_TO_HPRCOM_VOL             = 68,
  REG_PGAR_TO_HPRCOM_VOL              = 70,
  REG_DACR1_TO_HPRCOM_VOL             = 71,
  REG_HPRCOM_CTRL                     = 72,

  /* Left Line Output Plus/Minus Control registers */
  REG_PGAL_TO_LLOPM_VOL               = 81,
  REG_DACL1_TO_LLOPM_VOL              = 82,
  REG_PGAR_TO_LLOPM_VOL               = 84,
  REG_DACR1_TO_LLOPM_VOL              = 85,
  REG_LLOPM_CTRL                      = 86,

  /* Right Line Output Plus/Minus Control registers */
  REG_PGAL_TO_RLOPM_VOL               = 88,
  REG_DACL1_TO_RLOPM_VOL              = 89,
  REG_PGAR_TO_RLOPM_VOL               = 91,
  REG_DACR1_TO_RLOPM_VOL              = 92,
  REG_RLOPM_CTRL                      = 93,

  /* Module Power Status register */
  REG_POWER_STATUS                    = 94,
  /* Output Driver Short-Circuit Detection Status register */
  REG_SHORT_CIRCUIT_STATUS            = 95,

  /* Interrupt Flags registers */
  REG_STICKY_IRQ_FLAGS                = 96,
  REG_RT_IRQ_FLAGS                    = 97,

  /* Clock register */
  REG_CLOCK_SELECT                    = 101,
  /* Clock Generation Control register */
  REG_CLOCK_GENERATION_CTRL           = 102,

  /* New AGC registers */
  REG_NEW_LAGC_ATTACK                 = 103,
  REG_NEW_LAGC_DECAY                  = 104,
  REG_NEW_RAGC_ATTACK                 = 105,
  REG_NEW_RAGC_DECAY                  = 106,
  /* New Programmable ADC Digital Path and I2C Bus Condition register */
  REG_NEW_ADC_DIGITAL_PATH            = 107,
  /* Passive Analog Signal Bypass Selection During Powerdown register */
  REG_PASSIVE_BYPASS                  = 108,
  /* DAC Quiescent Current Adjustment register */
  REG_DAC_ICC_ADJ                     = 109
};
/*------------------Page Select register--------------------------------------*/
#define PAGE_SELECT_0                   0
#define PAGE_SELECT_1                   BIT(0)
/*------------------Software Reset register-----------------------------------*/
#define SOFTWARE_RESET_RESET            BIT(0)
/*------------------Codec Sample Rate Select register-------------------------*/
enum
{
  SAMPLE_RATE_DIV_NONE  = 0,
  SAMPLE_RATE_DIV_1_5   = 1,
  SAMPLE_RATE_DIV_2     = 2,
  SAMPLE_RATE_DIV_2_5   = 3,
  SAMPLE_RATE_DIV_3     = 4,
  SAMPLE_RATE_DIV_3_5   = 5,
  SAMPLE_RATE_DIV_4     = 6,
  SAMPLE_RATE_DIV_4_5   = 7,
  SAMPLE_RATE_DIV_5     = 8,
  SAMPLE_RATE_DIV_5_5   = 9,
  SAMPLE_RATE_DIV_6     = 10
};

#define SAMPLE_RATE_SELECT_DAC(value)   BIT_FIELD((value), 0)
#define SAMPLE_RATE_SELECT_DAC_MASK     BIT_FIELD(MASK(4), 0)
#define SAMPLE_RATE_SELECT_DAC_VALUE(reg) \
    FIELD_VALUE((reg), SAMPLE_RATE_SELECT_DAC_MASK, 0)

#define SAMPLE_RATE_SELECT_ADC(value)   BIT_FIELD((value), 4)
#define SAMPLE_RATE_SELECT_ADC_MASK     BIT_FIELD(MASK(4), 4)
#define SAMPLE_RATE_SELECT_ADC_VALUE(reg) \
    FIELD_VALUE((reg), SAMPLE_RATE_SELECT_ADC_MASK, 4)
/*------------------PLL Programming register A--------------------------------*/
#define PLLA_P(value)                   BIT_FIELD((value), 0)
#define PLLA_P_MASK                     BIT_FIELD(MASK(3), 0)
#define PLLA_P_VALUE(reg)               FIELD_VALUE((reg), PLLA_P_MASK, 0)

#define PLLA_Q(value)                   BIT_FIELD((value), 3)
#define PLLA_Q_MASK                     BIT_FIELD(MASK(4), 3)
#define PLLA_Q_VALUE(reg)               FIELD_VALUE((reg), PLLA_Q_MASK, 3)

#define PLLA_ENABLE                     BIT(7)
/*------------------PLL Programming register B--------------------------------*/
#define PLLB_J(value)                   BIT_FIELD((value), 2)
#define PLLB_J_MASK                     BIT_FIELD(MASK(6), 2)
#define PLLB_J_VALUE(reg)               FIELD_VALUE((reg), PLLB_J_MASK, 2)
/*------------------PLL Programming register C--------------------------------*/
/* 8 most significant bits */
#define PLLC_D(value)                   BIT_FIELD((value), 0)
#define PLLC_D_MASK                     BIT_FIELD(MASK(8), 0)
#define PLLC_D_VALUE(reg)               FIELD_VALUE((reg), PLLC_D_MASK, 0)
/*------------------PLL Programming register D--------------------------------*/
/* 6 least significant bits */
#define PLLD_D(value)                   BIT_FIELD((value), 2)
#define PLLD_D_MASK                     BIT_FIELD(MASK(6), 2)
#define PLLD_D_VALUE(reg)               FIELD_VALUE((reg), PLLD_D_MASK, 2)
/*------------------Codec Data-Path Setup register----------------------------*/
enum
{
  DAC_PATH_MUTED  = 0,
  DAC_PATH_RIGHT  = 1,
  DAC_PATH_LEFT   = 2,
  DAC_PATH_MONO   = 3
};

#define DATA_PATH_SETUP_RDAC(value)     BIT_FIELD((value), 1)
#define DATA_PATH_SETUP_RDAC_MASK       BIT_FIELD(MASK(2), 1)
#define DATA_PATH_SETUP_RDAC_VALUE(reg) \
    FIELD_VALUE((reg), DATA_PATH_SETUP_RDAC_MASK, 1)

#define DATA_PATH_SETUP_LDAC(value)     BIT_FIELD((value), 3)
#define DATA_PATH_SETUP_LDAC_MASK       BIT_FIELD(MASK(2), 3)
#define DATA_PATH_SETUP_LDAC_VALUE(reg) \
    FIELD_VALUE((reg), DATA_PATH_SETUP_LDAC_MASK, 3)

#define DATA_PATH_SETUP_DAC_DUAL_RATE   BIT(5)
#define DATA_PATH_SETUP_ADC_DUAL_RATE   BIT(6)

#define DATA_PATH_SETUP_44K1            BIT(7)
#define DATA_PATH_SETUP_48K             0
/*------------------Audio Serial Data Interface Control register A------------*/
/* Enable 3D Digital Effects */
#define ASDA_3D_EFFECT_CONTROL          BIT(2)
/* Enables BCLK/WCLK continuous transmission in idle and power-down modes */
#define ASDA_BCLK_WCLK_CONTROL          BIT(4)
/* Place DOUT in high-impedance state in idle mode */
#define ASDA_DOUT_3_STATE_CONTROL       BIT(5)

#define ASDA_WCLK_DIR_INPUT             0
#define ASDA_WCLK_DIR_OUTPUT            BIT(6)

#define ASDA_BCLK_DIR_INPUT             0
#define ASDA_BCLK_DIR_OUTPUT            BIT(7)
/*------------------Audio Serial Data Interface Control register B------------*/
enum
{
  INTERFACE_MODE_I2S            = 0,
  INTERFACE_MODE_DSP            = 1,
  INTERFACE_MODE_RIGHT_ALIGNED  = 2,
  INTERFACE_MODE_LEFT_ALIGNED   = 3
};

enum
{
  WORD_LENGTH_16  = 0,
  WORD_LENGTH_20  = 1,
  WORD_LENGTH_24  = 2,
  WORD_LENGTH_32  = 3
};

/* Enable soft-muting during ADC/DAC resync */
#define ASDB_RESYNC_MUTE_CONTROL        BIT(0)

#define ASDB_ADC_RESYNC_ENABLE          BIT(1)
#define ASDB_DAC_RESYNC_ENABLE          BIT(2)

/* Enable 256-clock transfer mode in master mode */
#define ASDB_BCLK_RATE_CONTROL          BIT(3)

#define ASDB_WORD_LENGTH(value)         BIT_FIELD((value), 4)
#define ASDB_WORD_LENGTH_MASK           BIT_FIELD(MASK(2), 4)
#define ASDB_WORD_LENGTH_VALUE(reg) \
    FIELD_VALUE((reg), ASDB_WORD_LENGTH_MASK, 4)

#define ASDB_INTERFACE_MODE(value)      BIT_FIELD((value), 6)
#define ASDB_INTERFACE_MODE_MASK        BIT_FIELD(MASK(2), 6)
#define ASDB_INTERFACE_MODE_VALUE(reg) \
    FIELD_VALUE((reg), ASDB_INTERFACE_MODE_MASK, 6)
/*------------------PLL R programming register and Codec Overflow flags-------*/
#define PLLR_R(value)                   BIT_FIELD((value), 0)
#define PLLR_R_MASK                     BIT_FIELD(MASK(4), 0)
#define PLLR_R_VALUE(reg)               FIELD_VALUE((reg), PLLR_R_MASK, 0)

#define PLLR_RDAC_OVF                   BIT(4)
#define PLLR_LDAC_OVF                   BIT(5)
#define PLLR_RADC_OVF                   BIT(6)
#define PLLR_LADC_OVF                   BIT(7)
/*------------------ADC PGA Gain Control registers----------------------------*/
#define ADC_PGA_GAIN(value)             BIT_FIELD((value), 0)
#define ADC_PGA_GAIN_MASK               BIT_FIELD(MASK(7), 0)
#define ADC_PGA_GAIN_VALUE(reg)         FIELD_VALUE((reg), ADC_PGA_GAIN_MASK, 0)

#define ADC_PGA_MUTE                    BIT(7)
/*------------------MIC L/R and LINE L/R Control registers--------------------*/
#define MIC_LINE_GAIN_DISABLED          15

#define MIC_LINE_R_GAIN(value)          BIT_FIELD((value), 0)
#define MIC_LINE_R_GAIN_MASK            BIT_FIELD(MASK(4), 0)
#define MIC_LINE_R_GAIN_VALUE(reg) \
    FIELD_VALUE((reg), MIC_LINE_R_GAIN_MASK, 0)

#define MIC_LINE_L_GAIN(value)          BIT_FIELD((value), 4)
#define MIC_LINE_L_GAIN_MASK            BIT_FIELD(MASK(4), 4)
#define MIC_LINE_L_GAIN_VALUE(reg) \
    FIELD_VALUE((reg), MIC_LINE_L_GAIN_MASK, 4)
/*------------------MIC LP/RP and LINE LP/RP Control registers----------------*/
enum
{
  MIC_LINE_SOFT_STEPPING_SAMPLES_1  = 0,
  MIC_LINE_SOFT_STEPPING_SAMPLES_2  = 1,
  MIC_LINE_SOFT_STEPPING_DISABLED   = 2
};

#define MIC_LINE_LP_RP_SOFT_STEPPING(value) \
    BIT_FIELD((value), 0)
#define MIC_LINE_LP_RP_SOFT_STEPPING_MASK \
    BIT_FIELD(MASK(2), 0)
#define MIC_LINE_LP_RP_SOFT_STEPPING_VALUE(reg) \
    FIELD_VALUE((reg), MIC_LINE_LP_RP_SOFT_STEPPING_MASK, 0)

#define MIC_LINE_LP_RP_ENABLE           BIT(2)

#define MIC_LINE_LP_RP_GAIN(value)      BIT_FIELD((value), 3)
#define MIC_LINE_LP_RP_GAIN_MASK        BIT_FIELD(MASK(4), 3)
#define MIC_LINE_LP_RP_GAIN_VALUE(reg) \
    FIELD_VALUE((reg), MIC_LINE_LP_RP_GAIN_MASK, 3)

#define MIC_LINE_LP_RP_DIFF             BIT(7)
/*------------------Microphone Bias Control register--------------------------*/
enum
{
  MICBIAS_DISABLED      = 0,
  MICBIAS_VOLTAGE_2V0   = 1,
  MICBIAS_VOLTAGE_2V5   = 2,
  MICBIAS_VOLTAGE_AVDD  = 3
};

#define MICBIAS_LEVEL(value)            BIT_FIELD((value), 6)
#define MICBIAS_LEVEL_MASK              BIT_FIELD(MASK(2), 6)
#define MICBIAS_LEVEL_VALUE(reg) \
    FIELD_VALUE((reg), MICBIAS_LEVEL_MASK, 6)
/*------------------AGC Control register A------------------------------------*/
enum
{
  ATTACK_TIME_8MS   = 0,
  ATTACK_TIME_11MS  = 1,
  ATTACK_TIME_16MS  = 2,
  ATTACK_TIME_20MS  = 3
};

enum
{
  DECAY_TIME_100MS  = 0,
  DECAY_TIME_200MS  = 1,
  DECAY_TIME_400MS  = 2,
  DECAY_TIME_500MS  = 3
};

enum
{
  TARGET_LEVEL_5P5DB  = 0,
  TARGET_LEVEL_8DB    = 1,
  TARGET_LEVEL_10DB   = 2,
  TARGET_LEVEL_12DB   = 3,
  TARGET_LEVEL_14DB   = 4,
  TARGET_LEVEL_17DB   = 5,
  TARGET_LEVEL_20DB   = 6,
  TARGET_LEVEL_24DB   = 7
};

#define AGC_CTRL_A_DECAY_TIME(value)    BIT_FIELD((value), 0)
#define AGC_CTRL_A_DECAY_TIME_MASK      BIT_FIELD(MASK(2), 0)
#define AGC_CTRL_A_DECAY_TIME_VALUE(reg) \
    FIELD_VALUE((reg), AGC_CTRL_A_DECAY_TIME_MASK, 0)

#define AGC_CTRL_A_ATTACK_TIME(value)   BIT_FIELD((value), 2)
#define AGC_CTRL_A_ATTACK_TIME_MASK     BIT_FIELD(MASK(2), 2)
#define AGC_CTRL_A_ATTACK_TIME_VALUE(reg) \
    FIELD_VALUE((reg), AGC_CTRL_A_ATTACK_TIME_MASK, 2)

#define AGC_CTRL_A_TARGET_LEVEL(value)  BIT_FIELD((value), 4)
#define AGC_CTRL_A_TARGET_LEVEL_MASK    BIT_FIELD(MASK(3), 4)
#define AGC_CTRL_A_TARGET_LEVEL_VALUE(reg) \
    FIELD_VALUE((reg), AGC_CTRL_A_TARGET_LEVEL_MASK, 4)

#define AGC_CTRL_A_ENABLE               BIT(7)
/*------------------AGC Control register B------------------------------------*/
#define AGC_CTRL_B_MAX_GAIN(value)      BIT_FIELD((value), 1)
#define AGC_CTRL_B_MAX_GAIN_MASK        BIT_FIELD(MASK(7), 1)
#define AGC_CTRL_B_MAX_GAIN_VALUE(reg) \
    FIELD_VALUE((reg), AGC_CTRL_B_MAX_GAIN_MASK, 1)
#define AGC_CTRL_B_MAX_GAIN_MAX         127
/*------------------AGC Control register C------------------------------------*/
#define AGC_CTRL_C_CLIP_STEPPING        BIT(0)

#define AGC_CTRL_C_NOISE_THRESHOLD(value) \
    BIT_FIELD((value), 1)
#define AGC_CTRL_C_NOISE_THRESHOLD_MASK \
    BIT_FIELD(MASK(5), 1)
#define AGC_CTRL_C_NOISE_THRESHOLD_VALUE(reg) \
    FIELD_VALUE((reg), AGC_CTRL_C_NOISE_THRESHOLD_MASK, 1)

#define AGC_CTRL_C_NOISE_HYSTERESIS(value) \
    BIT_FIELD((value), 6)
#define AGC_CTRL_C_NOISE_HYSTERESIS_MASK \
    BIT_FIELD(MASK(2), 6)
#define AGC_CTRL_C_NOISE_HYSTERESIS_VALUE(reg) \
    FIELD_VALUE((reg), AGC_CTRL_C_NOISE_HYSTERESIS_MASK, 6)
/*------------------ADC Flag register-----------------------------------------*/
#define ADC_FLAGS_RAGC_SATURATED        BIT(0)
#define ADC_FLAGS_RAGC_SIGNAL_LOW       BIT(1)
#define ADC_FLAGS_RADC_ENABLED          BIT(2)
#define ADC_FLAGS_RADC_PGA_GAIN_EQUAL   BIT(3)
#define ADC_FLAGS_LAGC_SATURATED        BIT(4)
#define ADC_FLAGS_LAGC_SIGNAL_LOW       BIT(5)
#define ADC_FLAGS_LADC_ENABLED          BIT(6)
#define ADC_FLAGS_LADC_PGA_GAIN_EQUAL   BIT(7)
/*------------------DAC Power and Output Driver Control register--------------*/
enum
{
  HPLCOM_OUTPUT_HPLOUT_DIFF   = 0,
  HPLCOM_OUTPUT_CONSTANT_VCM  = 1,
  HPLCOM_OUTPUT_SINGLE_ENDED  = 2
};

#define HPLCOM_OUTPUT(value)            BIT_FIELD((value), 4)
#define HPLCOM_OUTPUT_MASK              BIT_FIELD(MASK(2), 4)
#define HPLCOM_OUTPUT_VALUE(reg) \
    FIELD_VALUE((reg), HPLCOM_OUTPUT_MASK, 4)

#define HPLCOM_RDAC_POWER_CONTROL       BIT(6)
#define HPLCOM_LDAC_POWER_CONTROL       BIT(7)
/*------------------High-Power Output Drive Control register------------------*/
enum
{
  HPRCOM_OUTPUT_HPROUT_DIFF                 = 0,
  HPRCOM_OUTPUT_CONSTANT_VCM                = 1,
  HPRCOM_OUTPUT_SINGLE_ENDED                = 2,
  HPRCOM_OUTPUT_HPLCOM_DIFF                 = 3,
  HPRCOM_OUTPUT_EXT_FB_HPLCOM_CONSTANT_VCM  = 4
};

/* Short-Circuit Protection Mode Control */
#define HPRCOM_CFG_SC_POWER_OFF         BIT(1)
#define HPRCOM_CFG_SC_LIMIT             0

#define HPRCOM_CFG_SC_ENABLE            BIT(2)

#define HPRCOM_OUTPUT(value)            BIT_FIELD((value), 3)
#define HPRCOM_OUTPUT_MASK              BIT_FIELD(MASK(3), 3)
#define HPRCOM_OUTPUT_VALUE(reg) \
    FIELD_VALUE((reg), HPRCOM_OUTPUT_MASK, 3)
/*------------------High-Power Output Stage Control register------------------*/
enum
{
  OCM_VOLTAGE_1V35  = 0,
  OCM_VOLTAGE_1V5   = 1,
  OCM_VOLTAGE_1V65  = 2,
  OCM_VOLTAGE_1V8   = 3
};

enum
{
  OCMV_SOFT_STEPPING_SAMPLES_1  = 0,
  OCMV_SOFT_STEPPING_SAMPLES_2  = 1,
  OCMV_SOFT_STEPPING_DISABLED   = 2
};

#define HPOUT_SC_SOFT_STEPPING(value)   BIT_FIELD((value), 0)
#define HPOUT_SC_SOFT_STEPPING_MASK     BIT_FIELD(MASK(2), 0)
#define HPOUT_SC_SOFT_STEPPING_VALUE(reg) \
    FIELD_VALUE((reg), HPOUT_SC_SOFT_STEPPING_MASK, 0)

#define HPOUT_SC_VOLTAGE(value)         BIT_FIELD((value), 6)
#define HPOUT_SC_VOLTAGE_MASK           BIT_FIELD(MASK(2), 6)
#define HPOUT_SC_VOLTAGE_VALUE(reg) \
    FIELD_VALUE((reg), HPOUT_SC_VOLTAGE_MASK, 6)
/*------------------DAC Output Switching Control register---------------------*/
enum
{
  DAC_VOLUME_INDEPENDENT  = 0,
  DAC_VOLUME_RDAC         = 1,
  DAC_VOLUME_LDAC         = 2
};

enum
{
  DAC_MUX_1             = 0,
  DAC_MUX_3_LINE_OUTPUT = 1,
  DAC_MUX_2_HP_OUTPUT   = 2
};

#define DAC_MUX_VOLUME_CONTROL(value)   BIT_FIELD((value), 0)
#define DAC_MUX_VOLUME_CONTROL_MASK     BIT_FIELD(MASK(2), 0)
#define DAC_MUX_VOLUME_CONTROL_VALUE(reg) \
    FIELD_VALUE((reg), DAC_MUX_VOLUME_CONTROL_MASK, 0)

#define DAC_MUX_RDAC_CONTROL(value)     BIT_FIELD((value), 4)
#define DAC_MUX_RDAC_CONTROL_MASK       BIT_FIELD(MASK(2), 4)
#define DAC_MUX_RDAC_CONTROL_VALUE(reg) \
    FIELD_VALUE((reg), DAC_MUX_RDAC_CONTROL_MASK, 4)

#define DAC_MUX_LDAC_CONTROL(value)     BIT_FIELD((value), 6)
#define DAC_MUX_LDAC_CONTROL_MASK       BIT_FIELD(MASK(2), 6)
#define DAC_MUX_LDAC_CONTROL_VALUE(reg) \
    FIELD_VALUE((reg), DAC_MUX_LDAC_CONTROL_MASK, 6)
/*------------------DAC Digital Volume Control registers----------------------*/
#define DAC_DIGITAL_VOL_GAIN(value)     BIT_FIELD((value), 0)
#define DAC_DIGITAL_VOL_GAIN_MASK       BIT_FIELD(MASK(7), 0)
#define DAC_DIGITAL_VOL_GAIN_VALUE(reg) \
    FIELD_VALUE((reg), DAC_DIGITAL_VOL_GAIN_MASK, 0)

#define DAC_DIGITAL_VOL_MUTE            BIT(7)
/*------------------HPCOM, HPOUT, LOP/M output level control registers--------*/
#define OUTPUT_POWER_CONTROL            BIT(0)
#define OUTPUT_VOLUME_STATUS            BIT(1)
#define OUTPUT_POWER_DOWN_CONTROL       BIT(2)
#define OUTPUT_UNMUTE                   BIT(3)

#define OUTPUT_GAIN(value)              BIT_FIELD((value), 4)
#define OUTPUT_GAIN_MASK                BIT_FIELD(MASK(4), 4)
#define OUTPUT_GAIN_VALUE(reg) \
    FIELD_VALUE((reg), OUTPUT_GAIN_MASK, 4)
/*------------------DAC and PGA volume control registers----------------------*/
#define DAC_PGA_ANALOG_VOL_GAIN(value)  BIT_FIELD((value), 0)
#define DAC_PGA_ANALOG_VOL_GAIN_MASK    BIT_FIELD(MASK(7), 0)
#define DAC_PGA_ANALOG_VOL_GAIN_VALUE(reg) \
    FIELD_VALUE((reg), DAC_PGA_ANALOG_VOL_GAIN_MASK, 0)
#define DAC_PGA_ANALOG_VOL_GAIN_MUTE    127

#define DAC_PGA_ANALOG_VOL_UNMUTE       BIT(7)
/*------------------Module Power Status register------------------------------*/
#define POWER_STATUS_HPROUT_ENABLED     BIT(1)
#define POWER_STATUS_HPLOUT_ENABLED     BIT(2)
#define POWER_STATUS_RLOPM_ENABLED      BIT(3)
#define POWER_STATUS_LLOPM_ENABLED      BIT(4)
#define POWER_STATUS_RDAC_ENABLED       BIT(6)
#define POWER_STATUS_LDAC_ENABLED       BIT(7)
/*----------------------------------------------------------------------------*/
#endif /* DPM_AUDIO_TLV320AIC3X_DEFS_H_ */
