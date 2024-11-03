/*
 * tlv320aic3x.c
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/audio/tlv320aic3x.h>
#include <dpm/audio/tlv320aic3x_defs.h>
#include <halm/generic/i2c.h>
#include <halm/generic/work_queue.h>
#include <halm/timer.h>
#include <xcore/accel.h>
#include <xcore/atomic.h>
#include <assert.h>
#include <stdlib.h>
/*----------------------------------------------------------------------------*/
#define CHANNEL_MASK      (CHANNEL_LEFT | CHANNEL_RIGHT)
#define DEFAULT_RW_LENGTH 1

enum [[gnu::packed]] ActionGroup
{
  GROUP_RESET         = 0x0001,
  GROUP_GENERIC       = 0x0002,
  GROUP_RATE          = 0x0004,
  GROUP_PATH          = 0x0008,
  GROUP_INPUT         = 0x0010,
  GROUP_INPUT_LEVEL   = 0x0020,
  GROUP_OUTPUT        = 0x0040,
  GROUP_OUTPUT_LEVEL  = 0x0080,
  GROUP_CHECK         = 0x0100
};

#define GROUP_READY_MASK (GROUP_RESET | GROUP_GENERIC | GROUP_RATE \
    | GROUP_PATH | GROUP_INPUT | GROUP_OUTPUT)

enum [[gnu::packed]] CheckStep
{
  CHECK_GROUP_GENERIC,
  CHECK_ADC_FLAGS = CHECK_GROUP_GENERIC,
  CHECK_AGC_GAIN,
  CHECK_POWER_STATUS,

  /* Completion state */
  CHECK_END
};

enum [[gnu::packed]] ConfigStep
{
  /* Hardware reset */
  CONFIG_RESET,
  CONFIG_RESET_WAIT,
  CONFIG_END_RESET,

  /* Generic configuration */
  CONFIG_GROUP_GENERIC,
  CONFIG_ASD_IF_CTRL_A = CONFIG_GROUP_GENERIC,
  CONFIG_ASD_IF_CTRL_B,
  CONFIG_END_GROUP_GENERIC,

  /* Rate configuration */
  CONFIG_GROUP_RATE,
  CONFIG_PLLB = CONFIG_GROUP_RATE,
  CONFIG_PLLC,
  CONFIG_CODEC_OVERFLOW_PLL_R,
  CONFIG_PLLA,
  CONFIG_SAMPLE_RATE,
  CONFIG_END_GROUP_RATE,

  /* Data path setup */
  CONFIG_GROUP_PATH,
  CONFIG_CODEC_DATA_PATH_SETUP = CONFIG_GROUP_PATH,
  CONFIG_END_GROUP_PATH,

  /* Input path setup */
  CONFIG_GROUP_INPUT,
  CONFIG_MIC2LR_LINE2LR_TO_LADC_CTRL = CONFIG_GROUP_INPUT,
  CONFIG_MIC2LR_LINE2LR_TO_RADC_CTRL,
  CONFIG_MIC1LP_LINE1LP_TO_LADC_CTRL,
  CONFIG_MIC1RP_LINE1RP_TO_LADC_CTRL,
  CONFIG_MIC1RP_LINE1RP_TO_RADC_CTRL,
  CONFIG_MIC1LP_LINE1LP_TO_RADC_CTRL,
  CONFIG_LINE2L_TO_LADC_CTRL,
  CONFIG_LINE2R_TO_RADC_CTRL,
  CONFIG_MICBIAS_CTRL,
  CONFIG_LAGC_CTRL_ALL,
  CONFIG_RAGC_CTRL_ALL,
  CONFIG_LADC_GAIN_CTRL,
  CONFIG_RADC_GAIN_CTRL,
  CONFIG_END_GROUP_INPUT,

  /* Input level setup */
  CONFIG_GROUP_INPUT_LEVEL,
  CONFIG_LADC_LAGC_CTRL = CONFIG_GROUP_INPUT_LEVEL,
  CONFIG_RADC_RAGC_CTRL,
  CONFIG_END_GROUP_INPUT_LEVEL,

  /* Output path setup */
  CONFIG_GROUP_OUTPUT,
  CONFIG_HPOUT_SC = CONFIG_GROUP_OUTPUT,
  CONFIG_HPRCOM_CFG,
  CONFIG_HPLCOM_CFG_DAC_PWR,
  CONFIG_DAC_MUX,
  CONFIG_LLOPM_CTRL,
  CONFIG_RLOPM_CTRL,
  CONFIG_LDAC_VOL,
  CONFIG_RDAC_VOL,
  CONFIG_DACL1_TO_HPLCOM_VOL,
  CONFIG_DACR1_TO_HPRCOM_VOL,
  CONFIG_DACL1_TO_HPLOUT_VOL,
  CONFIG_DACR1_TO_HPROUT_VOL,
  CONFIG_DACL1_TO_LLOPM_VOL,
  CONFIG_DACR1_TO_RLOPM_VOL,
  CONFIG_HPLCOM_CTRL,
  CONFIG_HPRCOM_CTRL,
  CONFIG_HPLOUT_CTRL,
  CONFIG_HPROUT_CTRL,
  CONFIG_END_GROUP_OUTPUT,

  /* Output level setup */
  CONFIG_GROUP_OUTPUT_LEVEL,
  CONFIG_LOUT_VOL = CONFIG_GROUP_OUTPUT_LEVEL,
  CONFIG_ROUT_VOL,
  CONFIG_END_GROUP_OUTPUT_LEVEL,

  /* Completion states */
  CONFIG_READY_WAIT,
  CONFIG_END
};

enum [[gnu::packed]] State
{
  STATE_IDLE,

  STATE_CONFIG_START,
  STATE_CONFIG_UPDATE,
  STATE_CONFIG_TIMER_WAIT,
  STATE_CONFIG_SELECT_WAIT,
  STATE_CONFIG_REQUEST_WAIT,
  STATE_CONFIG_END,

  STATE_CHECK_START,
  STATE_CHECK_UPDATE,
  STATE_CHECK_SELECT_WAIT,
  STATE_CHECK_REQUEST_WAIT,
  STATE_CHECK_RESPONSE_WAIT,
  STATE_CHECK_PROCESS,
  STATE_CHECK_END,

  STATE_ERROR_WAIT,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
};
/*----------------------------------------------------------------------------*/
static inline uint8_t appliedAgcGainToLevel(int8_t);
static inline uint8_t levelToAnalogOutputGain(uint8_t);
static inline uint8_t levelToAnalogInputGain(uint8_t);
static inline enum ConfigStep groupIndexToConfigStep(unsigned int);
static inline bool isLastConfigGroupStep(enum ConfigStep);

static uint8_t makeRegAdcGainCtrl(const struct TLV320AIC3x *,
    enum CodecChannel);
static uint8_t makeRegAgcCtrlB(const struct TLV320AIC3x *, enum CodecChannel);
static uint8_t makeRegASDIfCtrlA(const struct TLV320AIC3x *);
static uint8_t makeRegASDIfCtrlB(const struct TLV320AIC3x *);
static uint8_t makeRegCodecDataPathSetup(const struct TLV320AIC3x *);
static uint8_t makeRegDacMux(const struct TLV320AIC3x *);
static uint8_t makeRegDacToHpComVol(const struct TLV320AIC3x *,
    enum CodecChannel);
static uint8_t makeRegDacToHpOutVol(const struct TLV320AIC3x *,
    enum CodecChannel);
static uint8_t makeRegDacToLOPMVol(const struct TLV320AIC3x *,
    enum CodecChannel);
static uint8_t makeRegDacVol(const struct TLV320AIC3x *, enum CodecChannel);
static uint8_t makeRegHpLCom(const struct TLV320AIC3x *);
static uint8_t makeRegHpRCom(const struct TLV320AIC3x *);
static uint8_t makeRegLOPMCtrl(const struct TLV320AIC3x *, enum CodecChannel);
static uint8_t makeRegHpComCtrl(const struct TLV320AIC3x *, enum CodecChannel);
static uint8_t makeRegHpOutCtrl(const struct TLV320AIC3x *, enum CodecChannel);
static uint8_t makeRegHpOutSC(const struct TLV320AIC3x *);
static uint8_t makeRegMicBiasCtrl(const struct TLV320AIC3x *);
static uint8_t makeRegMicLine1ToAdcCtrl(const struct TLV320AIC3x *,
    enum CodecChannel, enum CodecChannel);
static uint8_t makeRegMicLine2ToAdcCtrl(const struct TLV320AIC3x *,
    enum CodecChannel);
static uint8_t makeRegMicLine23ToAdcCtrl(const struct TLV320AIC3x *,
    enum CodecChannel);
static uint8_t makeRegSampleRateSelect(const struct TLV320AIC3x *);

static size_t makeRegAgcCtrlTransfer(const struct TLV320AIC3x *, uint8_t *,
    enum CodecChannel);
static size_t makeOutputVolTransfer(const struct TLV320AIC3x *, uint8_t *,
    enum CodecChannel);

static void busInit(struct TLV320AIC3x *);
static void busInitRead(struct TLV320AIC3x *);
static inline uint32_t calcBusTimeout(const struct Timer *);
static inline uint32_t calcResetTimeout(const struct Timer *);
static void changeRateConfig(struct TLV320AIC3x *, unsigned int);
static void invokeAction(struct TLV320AIC3x *, uint16_t);
static void invokeUpdate(struct TLV320AIC3x *);
static void onBusEvent(void *);
static void onTimerEvent(void *);
static bool processCheckResponse(struct TLV320AIC3x *);
static void startBusTimeout(struct Timer *);
static bool startCheckUpdate(struct TLV320AIC3x *, bool *);
static bool startConfigUpdate(struct TLV320AIC3x *);
static void updateTask(void *);
/*----------------------------------------------------------------------------*/
static enum Result aic3xInit(void *, const void *);
static void aic3xDeinit(void *);

static void aic3xCheck(void *);
static uint8_t aic3xGetInputGain(const void *, enum CodecChannel);
static enum CodecChannel aic3xGetInputMute(const void *);
static uint8_t aic3xGetOutputGain(const void *, enum CodecChannel);
static enum CodecChannel aic3xGetOutputMute(const void *);
static bool aic3xIsAGCEnabled(const void *);
static bool aic3xIsReady(const void *);
static void aic3xSetAGCEnabled(void *, bool);
static void aic3xSetInputGain(void *, enum CodecChannel, uint8_t);
static void aic3xSetInputMute(void *, enum CodecChannel);
static void aic3xSetInputPath(void *, int, enum CodecChannel);
static void aic3xSetOutputGain(void *, enum CodecChannel, uint8_t);
static void aic3xSetOutputMute(void *, enum CodecChannel);
static void aic3xSetOutputPath(void *, int, enum CodecChannel);
static void aic3xSetSampleRate(void *, uint32_t);
static void aic3xSetErrorCallback(void *, void (*)(void *), void *);
static void aic3xSetIdleCallback(void *, void (*)(void *), void *);
static void aic3xSetUpdateCallback(void *, void (*)(void *), void *);
static void aic3xSetUpdateWorkQueue(void *, struct WorkQueue *);
static void aic3xReset(void *);
static bool aic3xUpdate(void *);
/*----------------------------------------------------------------------------*/
const struct CodecClass * const TLV320AIC3x = &(const struct CodecClass){
    .size = sizeof(struct TLV320AIC3x),
    .init = aic3xInit,
    .deinit = aic3xDeinit,

    .getInputGain = aic3xGetInputGain,
    .getInputMute = aic3xGetInputMute,
    .getOutputGain = aic3xGetOutputGain,
    .getOutputMute = aic3xGetOutputMute,
    .isAGCEnabled = aic3xIsAGCEnabled,
    .isReady = aic3xIsReady,

    .setAGCEnabled = aic3xSetAGCEnabled,
    .setInputGain = aic3xSetInputGain,
    .setInputMute = aic3xSetInputMute,
    .setInputPath = aic3xSetInputPath,
    .setOutputGain = aic3xSetOutputGain,
    .setOutputMute = aic3xSetOutputMute,
    .setOutputPath = aic3xSetOutputPath,
    .setSampleRate = aic3xSetSampleRate,

    .setErrorCallback = aic3xSetErrorCallback,
    .setIdleCallback = aic3xSetIdleCallback,
    .setUpdateCallback = aic3xSetUpdateCallback,
    .setUpdateWorkQueue = aic3xSetUpdateWorkQueue,

    .check = aic3xCheck,
    .reset = aic3xReset,
    .update = aic3xUpdate
};
/*----------------------------------------------------------------------------*/
static inline uint8_t appliedAgcGainToLevel(int8_t gain)
{
  return (((int)gain + 24) * 458) / 256;
}
/*----------------------------------------------------------------------------*/
static inline uint8_t levelToAnalogOutputGain(uint8_t level)
{
  return ((255 - (unsigned int)level) * 118) / 256;
}
/*----------------------------------------------------------------------------*/
static inline uint8_t levelToAnalogInputGain(uint8_t level)
{
  return ((unsigned int)level * 120) / 256;
}
/*----------------------------------------------------------------------------*/
static inline enum ConfigStep groupIndexToConfigStep(unsigned int index)
{
  static const enum ConfigStep indexToGroupMap[] = {
      CONFIG_RESET,
      CONFIG_GROUP_GENERIC,
      CONFIG_GROUP_RATE,
      CONFIG_GROUP_PATH,
      CONFIG_GROUP_INPUT,
      CONFIG_GROUP_INPUT_LEVEL,
      CONFIG_GROUP_OUTPUT,
      CONFIG_GROUP_OUTPUT_LEVEL
  };

  assert(index < ARRAY_SIZE(indexToGroupMap));
  return indexToGroupMap[index];
}
/*----------------------------------------------------------------------------*/
static inline bool isLastConfigGroupStep(enum ConfigStep step)
{
  return step == CONFIG_END
      || step == CONFIG_END_RESET
      || step == CONFIG_END_GROUP_GENERIC
      || step == CONFIG_END_GROUP_RATE
      || step == CONFIG_END_GROUP_PATH
      || step == CONFIG_END_GROUP_INPUT
      || step == CONFIG_END_GROUP_INPUT_LEVEL
      || step == CONFIG_END_GROUP_OUTPUT
      || step == CONFIG_END_GROUP_OUTPUT_LEVEL;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegAdcGainCtrl(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  if (codec->config.input.path != AIC3X_NONE
      && (codec->config.input.channels & channel)
      && (codec->config.input.channels & codec->config.input.unmute))
  {
    const uint8_t gain = levelToAnalogInputGain(channel == CHANNEL_LEFT ?
        codec->config.input.maxGainL : codec->config.input.maxGainR);

    return ADC_PGA_GAIN(gain);
  }

  return ADC_PGA_MUTE;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegAgcCtrlB(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  uint8_t gain;

  if (codec->config.input.path != AIC3X_NONE && codec->config.input.agc
      && (codec->config.input.channels & channel))
  {
    gain = levelToAnalogInputGain(channel == CHANNEL_LEFT ?
        codec->config.input.maxGainL : codec->config.input.maxGainR);
  }
  else
  {
    gain = AGC_CTRL_B_MAX_GAIN_MAX;
  }

  return AGC_CTRL_B_MAX_GAIN(gain);
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegASDIfCtrlA(const struct TLV320AIC3x *)
{
  return ASDA_DOUT_3_STATE_CONTROL;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegASDIfCtrlB(const struct TLV320AIC3x *)
{
  return ASDB_INTERFACE_MODE(INTERFACE_MODE_I2S)
      | ASDB_WORD_LENGTH(WORD_LENGTH_16);
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegCodecDataPathSetup(const struct TLV320AIC3x *codec)
{
  uint8_t value = 0;

  if (codec->config.output.path != AIC3X_NONE)
  {
    const unsigned int dir = codec->swap ? DAC_PATH_SWAP : DAC_PATH_SAME;

    if (codec->config.output.channels & CHANNEL_LEFT)
      value |= DATA_PATH_SETUP_LDAC(dir);
    if (codec->config.output.channels & CHANNEL_RIGHT)
      value |= DATA_PATH_SETUP_RDAC(dir);
  }

  if (codec->config.rate > 48000)
    value |= DATA_PATH_SETUP_DAC_DUAL_RATE | DATA_PATH_SETUP_ADC_DUAL_RATE;

  if (!(codec->config.rate % 48000))
    value |= DATA_PATH_SETUP_48K;
  else
    value |= DATA_PATH_SETUP_44K1;

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegDacMux(const struct TLV320AIC3x *codec)
{
  uint8_t value = DAC_MUX_VOLUME_CONTROL(DAC_VOLUME_INDEPENDENT);

  if (codec->config.output.path != AIC3X_NONE)
    value |= DAC_MUX_RDAC_CONTROL(DAC_MUX_1) | DAC_MUX_LDAC_CONTROL(DAC_MUX_1);

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegDacToHpComVol(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  uint8_t value = 0;

  switch (codec->config.output.path)
  {
    case AIC3X_HP_COM:
      if (codec->config.output.channels & channel)
      {
        const uint8_t gain = levelToAnalogOutputGain(channel == CHANNEL_LEFT ?
            codec->config.output.gainL : codec->config.output.gainR);

        value = DAC_PGA_ANALOG_VOL_GAIN(gain) | DAC_PGA_ANALOG_VOL_UNMUTE;
      }
      break;

    default:
      break;
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegDacToHpOutVol(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  uint8_t value = 0;

  switch (codec->config.output.path)
  {
    case AIC3X_HP_OUT:
    case AIC3X_HP_OUT_DIFF:
      if (codec->config.output.channels & channel)
      {
        const uint8_t gain = levelToAnalogOutputGain(channel == CHANNEL_LEFT ?
            codec->config.output.gainL : codec->config.output.gainR);

        value = DAC_PGA_ANALOG_VOL_GAIN(gain) | DAC_PGA_ANALOG_VOL_UNMUTE;
      }
      break;

    default:
      break;
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegDacToLOPMVol(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  uint8_t value = 0;

  switch (codec->config.output.path)
  {
    case AIC3X_LINE_OUT:
    case AIC3X_LINE_OUT_DIFF:
      if (codec->config.output.channels & channel)
      {
        const uint8_t gain = levelToAnalogOutputGain(channel == CHANNEL_LEFT ?
            codec->config.output.gainL : codec->config.output.gainR);

        value = DAC_PGA_ANALOG_VOL_GAIN(gain) | DAC_PGA_ANALOG_VOL_UNMUTE;
      }
      break;

    default:
      break;
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegDacVol(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  if (codec->config.output.path != AIC3X_NONE
      && (codec->config.output.channels & channel))
  {
    return DAC_DIGITAL_VOL_GAIN(0);
  }
  else
    return DAC_DIGITAL_VOL_MUTE;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpLCom(const struct TLV320AIC3x *codec)
{
  uint8_t value = 0;

  if (codec->config.output.path != AIC3X_NONE)
  {
    if (codec->config.output.channels & CHANNEL_LEFT)
      value |= HPLCOM_LDAC_POWER_CONTROL;
    if (codec->config.output.channels & CHANNEL_RIGHT)
      value |= HPLCOM_RDAC_POWER_CONTROL;
  }

  if (codec->config.output.channels & CHANNEL_LEFT)
  {
    switch (codec->config.output.path)
    {
      case AIC3X_HP_COM:
        value |= HPLCOM_OUTPUT(HPLCOM_OUTPUT_SINGLE_ENDED);
        break;

      case AIC3X_HP_OUT:
        value |= HPLCOM_OUTPUT(HPLCOM_OUTPUT_CONSTANT_VCM);
        break;

      case AIC3X_HP_OUT_DIFF:
        value |= HPLCOM_OUTPUT(HPLCOM_OUTPUT_HPLOUT_DIFF);
        break;

      default:
        break;
    }
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpRCom(const struct TLV320AIC3x *codec)
{
  uint8_t value = 0;

  if (codec->config.output.channels & CHANNEL_RIGHT)
  {
    value |= HPRCOM_CFG_SC_LIMIT | HPRCOM_CFG_SC_ENABLE;

    switch (codec->config.output.path)
    {
      case AIC3X_HP_COM:
        value |= HPRCOM_OUTPUT(HPRCOM_OUTPUT_SINGLE_ENDED);
        break;

      case AIC3X_HP_OUT:
        value |= HPRCOM_OUTPUT(HPRCOM_OUTPUT_CONSTANT_VCM);
        break;

      case AIC3X_HP_OUT_DIFF:
        value |= HPRCOM_OUTPUT(HPRCOM_OUTPUT_HPROUT_DIFF);
        break;

      default:
        value &= ~HPRCOM_CFG_SC_ENABLE;
        break;
    }
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegLOPMCtrl(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  uint8_t value = 0;

  switch (codec->config.output.path)
  {
    case AIC3X_LINE_OUT:
    case AIC3X_LINE_OUT_DIFF:
      if (codec->config.output.channels & channel)
      {
        /* 0 dB output level */
        value = OUTPUT_POWER_CONTROL | OUTPUT_UNMUTE | OUTPUT_GAIN(0);
      }
      break;

    default:
      break;
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpComCtrl(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  uint8_t value = 0;

  switch (codec->config.output.path)
  {
    case AIC3X_HP_COM:
      if (codec->config.output.channels & channel)
      {
        /* 9 dB output level */
        value = OUTPUT_POWER_CONTROL | OUTPUT_UNMUTE | OUTPUT_GAIN(9);
      }
      break;

    default:
      break;
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpOutCtrl(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  uint8_t value = 0;

  switch (codec->config.output.path)
  {
    case AIC3X_HP_OUT:
    case AIC3X_HP_OUT_DIFF:
      if (codec->config.output.channels & channel)
      {
        /* 9 dB output level */
        value = OUTPUT_POWER_CONTROL | OUTPUT_UNMUTE | OUTPUT_GAIN(9);
      }
      break;

    default:
      break;
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpOutSC(const struct TLV320AIC3x *codec)
{
  switch (codec->config.output.path)
  {
    case AIC3X_HP_COM:
    case AIC3X_HP_OUT:
    case AIC3X_HP_OUT_DIFF:
      return HPOUT_SC_VOLTAGE(OCM_VOLTAGE_1V5);

    default:
      return 0;
  }
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegMicBiasCtrl(const struct TLV320AIC3x *codec)
{
  switch (codec->config.input.path)
  {
    case AIC3X_MIC_1_IN:
    case AIC3X_MIC_1_IN_DIFF:
    case AIC3X_MIC_2_IN:
    case AIC3X_MIC_3_IN:
      return MICBIAS_LEVEL(MICBIAS_VOLTAGE_2V0);

    default:
      return 0;
  }
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegMicLine1ToAdcCtrl(const struct TLV320AIC3x *codec,
    enum CodecChannel source, enum CodecChannel destination)
{
  uint8_t value = MIC_LINE_LP_RP_GAIN(MIC_LINE_GAIN_DISABLED);

  if (codec->config.input.path != AIC3X_NONE
      && (codec->config.input.channels & source))
  {
    if (source == destination)
    {
      value |= MIC_LINE_LP_RP_SOFT_STEPPING(MIC_LINE_SOFT_STEPPING_DISABLED)
          | MIC_LINE_LP_RP_ENABLE;
    }

    /* swap && source != destination || !swap && source == destination */
    if (!(codec->swap ^ (source != destination)))
    {
      switch (codec->config.input.path)
      {
        case AIC3X_MIC_1_IN_DIFF:
        case AIC3X_LINE_1_IN_DIFF:
          value |= MIC_LINE_LP_RP_DIFF;
          [[fallthrough]];

        case AIC3X_MIC_1_IN:
        case AIC3X_LINE_1_IN:
          value = (value & ~MIC_LINE_LP_RP_GAIN_MASK) | MIC_LINE_LP_RP_GAIN(0);
          break;

        default:
          break;
      }
    }
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegMicLine2ToAdcCtrl(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  /*
   * MIC2/LINE2 for TLV320AIC3105, unused on TLV320AIC3101/3104.
   * Stereo channel swap function is not available for MIC2/LINE2 inputs.
   */

  uint8_t value = MIC_LINE_LP_RP_GAIN(MIC_LINE_GAIN_DISABLED);

  if (codec->config.input.path != AIC3X_NONE
      && (codec->config.input.channels & channel))
  {
    if (codec->type == AIC3X_TYPE_3101 || codec->type == AIC3X_TYPE_3105)
      value |= LINE2_WEAK_CM_BIAS_CONTROL;

    if (codec->type == AIC3X_TYPE_3105)
    {
      switch (codec->config.input.path)
      {
        case AIC3X_MIC_2_IN:
        case AIC3X_LINE_2_IN:
          value = (value & ~MIC_LINE_LP_RP_GAIN_MASK) | MIC_LINE_LP_RP_GAIN(0);
          break;

        default:
          break;
      }
    }
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegMicLine23ToAdcCtrl(const struct TLV320AIC3x *codec,
    enum CodecChannel channel)
{
  /* MIC2/LINE2 for TLV320AIC3101/3104 and MIC3/LINE3 for TLV320AIC3105  */

  uint8_t value = MIC_LINE_R_GAIN(MIC_LINE_GAIN_DISABLED)
      | MIC_LINE_L_GAIN(MIC_LINE_GAIN_DISABLED);

  if (codec->config.input.channels & channel)
  {
    bool enable;

    switch (codec->config.input.path)
    {
      case AIC3X_MIC_2_IN:
      case AIC3X_LINE_2_IN:
        enable = (codec->type != AIC3X_TYPE_3105);
        break;

      case AIC3X_MIC_3_IN:
      case AIC3X_LINE_3_IN:
        enable = (codec->type == AIC3X_TYPE_3105);
        break;

      default:
        enable = false;
        break;
    }

    if (enable)
    {
      static const uint8_t enableChannelL =
          MIC_LINE_R_GAIN(MIC_LINE_GAIN_DISABLED) | MIC_LINE_L_GAIN(0);
      static const uint8_t enableChannelR =
          MIC_LINE_R_GAIN(0) | MIC_LINE_L_GAIN(MIC_LINE_GAIN_DISABLED);

      if (channel == CHANNEL_LEFT)
        value = codec->swap ? enableChannelR : enableChannelL;
      else
        value = codec->swap ? enableChannelL : enableChannelR;
    }
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegSampleRateSelect(const struct TLV320AIC3x *codec)
{
  uint8_t value;

  if (codec->config.rate > 48000)
  {
    value = SAMPLE_RATE_SELECT_DAC(SAMPLE_RATE_DIV_2)
        | SAMPLE_RATE_SELECT_ADC(SAMPLE_RATE_DIV_2);
  }
  else
  {
    value = SAMPLE_RATE_SELECT_DAC(SAMPLE_RATE_DIV_NONE)
        | SAMPLE_RATE_SELECT_ADC(SAMPLE_RATE_DIV_NONE);
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static size_t makeRegAgcCtrlTransfer(const struct TLV320AIC3x *codec,
    uint8_t *buffer, enum CodecChannel channel)
{
  buffer[0] = channel == CHANNEL_LEFT ? REG_LAGC_CTRL_A : REG_RAGC_CTRL_A;
  buffer[2] = makeRegAgcCtrlB(codec, channel);

  if (codec->config.input.path != AIC3X_NONE && codec->config.input.agc)
  {
    buffer[1] = codec->config.input.agcControlA;
    buffer[3] = codec->config.input.agcControlC;

    if (codec->config.input.channels & channel)
      buffer[1] |= AGC_CTRL_A_ENABLE;
  }
  else
  {
    buffer[1] = 0;
    buffer[3] = 0;
  }

  return 4;
}
/*----------------------------------------------------------------------------*/
static size_t makeOutputVolTransfer(const struct TLV320AIC3x *codec,
    uint8_t *buffer, enum CodecChannel channel)
{
  assert(codec->config.output.path != AIC3X_NONE);

  const uint8_t gain = levelToAnalogOutputGain(channel == CHANNEL_LEFT ?
      codec->config.output.gainL : codec->config.output.gainR);

  switch (codec->config.output.path)
  {
    case AIC3X_HP_COM:
      buffer[0] = channel == CHANNEL_LEFT ?
          REG_DACL1_TO_HPLCOM_VOL : REG_DACR1_TO_HPRCOM_VOL;
      break;

    case AIC3X_HP_OUT:
    case AIC3X_HP_OUT_DIFF:
      buffer[0] = channel == CHANNEL_LEFT ?
          REG_DACL1_TO_HPLOUT_VOL : REG_DACR1_TO_HPROUT_VOL;
      break;

    default:
      buffer[0] = channel == CHANNEL_LEFT ?
          REG_DACL1_TO_LLOPM_VOL : REG_DACR1_TO_RLOPM_VOL;
      break;
  }

  buffer[1] = DAC_PGA_ANALOG_VOL_GAIN(gain);

  if ((codec->config.output.channels & channel)
      && (codec->config.output.channels & codec->config.output.unmute))
  {
    buffer[1] |= DAC_PGA_ANALOG_VOL_UNMUTE;
  }

  return 2;
}
/*----------------------------------------------------------------------------*/
static void busInit(struct TLV320AIC3x *codec)
{
  /* Lock the interface */
  ifSetParam(codec->bus, IF_ACQUIRE, NULL);

  ifSetParam(codec->bus, IF_ADDRESS, &codec->address);
  ifSetParam(codec->bus, IF_ZEROCOPY, NULL);
  ifSetCallback(codec->bus, onBusEvent, codec);

  if (codec->rate)
    ifSetParam(codec->bus, IF_RATE, &codec->rate);

  /* Start bus watchdog */
  startBusTimeout(codec->timer);
}
/*----------------------------------------------------------------------------*/
static void busInitRead(struct TLV320AIC3x *codec)
{
  /* Interface is already locked, just enable repeated start */
  ifSetParam(codec->bus, IF_I2C_REPEATED_START, NULL);

  /* Start bus watchdog */
  startBusTimeout(codec->timer);
}
/*----------------------------------------------------------------------------*/
static inline uint32_t calcBusTimeout(const struct Timer *timer)
{
  static const uint32_t busTimeoutFreq = 10;
  return (timerGetFrequency(timer) + busTimeoutFreq - 1) / busTimeoutFreq;
}
/*----------------------------------------------------------------------------*/
static inline uint32_t calcResetTimeout(const struct Timer *timer)
{
  static const uint32_t resetRequestFreq = 100;
  return (timerGetFrequency(timer) + resetRequestFreq - 1) / resetRequestFreq;
}
/*----------------------------------------------------------------------------*/
static void changeRateConfig(struct TLV320AIC3x *codec, unsigned int rate)
{
  codec->config.rate = rate;

  if (codec->config.pll.q != 0)
  {
    codec->config.pll.d = 0;
    codec->config.pll.j = 1; /* 0 is reserved value and should not be used */
    codec->config.pll.p = 0;
    codec->config.pll.r = 0;
    return;
  }

  switch (codec->config.rate)
  {
    case 22050:
      codec->config.pll.d = 5264;
      codec->config.pll.j = 7;
      codec->config.pll.p = 2;
      codec->config.pll.r = 1;
      break;

    case 48000:
      codec->config.pll.d = 1920;
      codec->config.pll.j = 8;
      codec->config.pll.p = 1;
      codec->config.pll.r = 1;
      break;

    default:
      /* Default is 44100 */
      codec->config.pll.d = 5264;
      codec->config.pll.j = 7;
      codec->config.pll.p = 1;
      codec->config.pll.r = 1;
      break;
  }
}
/*----------------------------------------------------------------------------*/
static void invokeAction(struct TLV320AIC3x *codec, uint16_t actions)
{
  const uint16_t previous = atomicFetchOr(&codec->transfer.groups, actions);

  if (previous == 0)
    invokeUpdate(codec);
}
/*----------------------------------------------------------------------------*/
static void invokeUpdate(struct TLV320AIC3x *codec)
{
  assert(codec->updateCallback != NULL || codec->wq != NULL);

  if (codec->updateCallback != NULL)
  {
    codec->updateCallback(codec->updateCallbackArgument);
  }
  else if (!codec->pending)
  {
    codec->pending = true;

    if (wqAdd(codec->wq, updateTask, codec) != E_OK)
      codec->pending = false;
  }
}
/*----------------------------------------------------------------------------*/
static void onBusEvent(void *object)
{
  struct TLV320AIC3x * const codec = object;
  bool busy = false;

  timerDisable(codec->timer);

  if (ifGetParam(codec->bus, IF_STATUS, NULL) != E_OK)
  {
    codec->transfer.state = STATE_ERROR_WAIT;

    /* Start bus timeout sequence */
    startBusTimeout(codec->timer);
  }

  switch (codec->transfer.state)
  {
    case STATE_CHECK_SELECT_WAIT:
      busy = true;
      codec->transfer.state = STATE_CHECK_REQUEST_WAIT;

      /* Reconfigure bus and enable watchdof */
      busInitRead(codec);

      /* Page 0 selected, write register address */
      ifWrite(codec->bus, codec->transfer.buffer, DEFAULT_RW_LENGTH);
      break;

    case STATE_CHECK_REQUEST_WAIT:
      busy = true;
      codec->transfer.state = STATE_CHECK_RESPONSE_WAIT;

      /* Start bus watchdog */
      startBusTimeout(codec->timer);

      /* Register selected, read data */
      ifRead(codec->bus, codec->transfer.buffer, codec->transfer.length);
      break;

    case STATE_CHECK_RESPONSE_WAIT:
      codec->transfer.state = STATE_CHECK_PROCESS;
      break;

    case STATE_CONFIG_SELECT_WAIT:
      busy = true;
      codec->transfer.state = STATE_CONFIG_REQUEST_WAIT;

      /* Start bus watchdog */
      startBusTimeout(codec->timer);

      /* Page 0 selected, start register write */
      ifWrite(codec->bus, codec->transfer.buffer, codec->transfer.length);
      break;

    case STATE_CONFIG_REQUEST_WAIT:
      codec->transfer.state = STATE_CONFIG_END;
      break;

    default:
      break;
  }

  if (!busy)
  {
    ifSetCallback(codec->bus, NULL, NULL);
    ifSetParam(codec->bus, IF_RELEASE, NULL);

    invokeUpdate(codec);
  }
}
/*----------------------------------------------------------------------------*/
static void onTimerEvent(void *object)
{
  struct TLV320AIC3x * const codec = object;

  switch (codec->transfer.state)
  {
    case STATE_CONFIG_TIMER_WAIT:
      codec->transfer.state = STATE_CONFIG_END;
      break;

    case STATE_ERROR_WAIT:
      codec->transfer.state = STATE_ERROR_INTERFACE;
      break;

    default:
      ifSetCallback(codec->bus, NULL, NULL);
      ifSetParam(codec->bus, IF_RELEASE, NULL);
      codec->transfer.state = STATE_ERROR_TIMEOUT;
      break;
  }

  invokeUpdate(codec);
}
/*----------------------------------------------------------------------------*/
static bool processCheckResponse(struct TLV320AIC3x *codec)
{
  const uint8_t * const response = codec->transfer.buffer;

  if (codec->transfer.groups & ~GROUP_CHECK)
  {
    /* Codec reconfigured, skip current check */
    return true;
  }

  switch (codec->transfer.step)
  {
    case CHECK_ADC_FLAGS:
    {
      uint8_t adcFlagsMask = 0;

      if (codec->config.input.channels & CHANNEL_LEFT)
        adcFlagsMask |= ADC_FLAGS_LADC_ENABLED;
      if (codec->config.input.channels & CHANNEL_RIGHT)
        adcFlagsMask |= ADC_FLAGS_RADC_ENABLED;

      return (response[0] & adcFlagsMask) == adcFlagsMask;
    }

    case CHECK_AGC_GAIN:
    {
      codec->config.input.gainL = appliedAgcGainToLevel((int8_t)response[0]);
      codec->config.input.gainR = appliedAgcGainToLevel((int8_t)response[1]);
      return true;
    }

    case CHECK_POWER_STATUS:
    {
      uint8_t powerStatusMask = 0;

      if (codec->config.output.channels & CHANNEL_LEFT)
        powerStatusMask |= POWER_STATUS_LDAC_ENABLED;
      if (codec->config.output.channels & CHANNEL_RIGHT)
        powerStatusMask |= POWER_STATUS_RDAC_ENABLED;

      return (response[0] & powerStatusMask) == powerStatusMask;
    }

    default:
      /* Unreachable state */
      return false;
  }
}
/*----------------------------------------------------------------------------*/
static void startBusTimeout(struct Timer *timer)
{
  timerSetOverflow(timer, calcBusTimeout(timer));
  timerSetValue(timer, 0);
  timerEnable(timer);
}
/*----------------------------------------------------------------------------*/
static bool startCheckUpdate(struct TLV320AIC3x *codec, bool *busy)
{
  size_t length = 0;

  if (codec->transfer.step == CHECK_ADC_FLAGS)
  {
    if (codec->config.input.path != AIC3X_NONE)
    {
      codec->transfer.buffer[0] = REG_ADC_FLAGS;
      length = 1;
    }
    else
    {
      /* Input path is disabled, skip ADC check */
      ++codec->transfer.step;
    }
  }

  if (codec->transfer.step == CHECK_AGC_GAIN)
  {
    if (codec->config.input.agc)
    {
      codec->transfer.buffer[0] = REG_LAGC_GAIN;
      length = 2;
    }
    else
    {
      /* Automatic gain control is disabled, skip AGC check */
      ++codec->transfer.step;
    }
  }

  if (codec->transfer.step == CHECK_POWER_STATUS)
  {
    if (codec->config.output.path != AIC3X_NONE)
    {
      codec->transfer.buffer[0] = REG_POWER_STATUS;
      length = 1;
    }
    else
    {
      /* Output path is disabled, skip DAC check */
      ++codec->transfer.step;
    }
  }

  if (codec->transfer.step != CHECK_END)
  {
    assert(length != 0);

    codec->transfer.length = length;
    codec->transfer.state = STATE_CHECK_SELECT_WAIT;

    busInit(codec);
    ifWrite(codec->bus, codec->transfer.page, sizeof(codec->transfer.page));

    *busy = true;
    return false;
  }
  else
  {
    codec->transfer.state = STATE_CHECK_END;

    *busy = false;
    return true;
  }
}
/*----------------------------------------------------------------------------*/
static bool startConfigUpdate(struct TLV320AIC3x *codec)
{
  size_t length = 2;
  uint32_t timeout = 0;

  switch (codec->transfer.step)
  {
    case CONFIG_RESET:
      pinReset(codec->reset);
      timeout = calcResetTimeout(codec->timer);
      break;

    case CONFIG_RESET_WAIT:
      pinSet(codec->reset);
      timeout = calcResetTimeout(codec->timer);
      break;

    case CONFIG_READY_WAIT:
      timeout = calcResetTimeout(codec->timer);
      break;

    /* Generic configuration */

    case CONFIG_ASD_IF_CTRL_A:
      codec->transfer.buffer[0] = REG_ASD_IF_CTRL_A;
      codec->transfer.buffer[1] = makeRegASDIfCtrlA(codec);
      break;

    case CONFIG_ASD_IF_CTRL_B:
      codec->transfer.buffer[0] = REG_ASD_IF_CTRL_B;
      codec->transfer.buffer[1] = makeRegASDIfCtrlB(codec);
      break;

    /* Rate configuration */

    case CONFIG_PLLB:
      codec->transfer.buffer[0] = REG_PLL_B;
      codec->transfer.buffer[1] = PLLB_J(codec->config.pll.j);
      break;

    case CONFIG_PLLC:
      codec->transfer.buffer[0] = REG_PLL_C;
      codec->transfer.buffer[1] = PLLD_D(codec->config.pll.d & 0x3F);
      codec->transfer.buffer[2] = PLLC_D((codec->config.pll.d >> 6) & 0xFF);
      length = 3;
      break;

    case CONFIG_CODEC_OVERFLOW_PLL_R:
      codec->transfer.buffer[0] = REG_CODEC_OVERFLOW_PLL_R;
      codec->transfer.buffer[1] = PLLR_R(codec->config.pll.r);
      break;

    case CONFIG_PLLA:
      codec->transfer.buffer[0] = REG_PLL_A;
      codec->transfer.buffer[1] = PLLA_P(codec->config.pll.p);

      if (codec->config.pll.q != 0)
        codec->transfer.buffer[1] |= PLLA_Q(codec->config.pll.q);
      else
        codec->transfer.buffer[1] |= PLLA_ENABLE;
      break;

    case CONFIG_SAMPLE_RATE:
      codec->transfer.buffer[0] = REG_SAMPLE_RATE_SELECT;
      codec->transfer.buffer[1] = makeRegSampleRateSelect(codec);
      break;

    /* Data path setup */

    case CONFIG_CODEC_DATA_PATH_SETUP:
      codec->transfer.buffer[0] = REG_CODEC_DATA_PATH_SETUP;
      codec->transfer.buffer[1] = makeRegCodecDataPathSetup(codec);
      break;

    /* Input path setup */

    case CONFIG_MIC2LR_LINE2LR_TO_LADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC2LR_LINE2LR_TO_LADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine23ToAdcCtrl(codec,
          CHANNEL_LEFT);
      break;

    case CONFIG_MIC2LR_LINE2LR_TO_RADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC2LR_LINE2LR_TO_RADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine23ToAdcCtrl(codec,
          CHANNEL_RIGHT);
      break;

    case CONFIG_MIC1LP_LINE1LP_TO_LADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC1LP_LINE1LP_TO_LADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine1ToAdcCtrl(codec,
          CHANNEL_LEFT, CHANNEL_LEFT);
      break;

    case CONFIG_MIC1RP_LINE1RP_TO_LADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC1RP_LINE1RP_TO_LADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine1ToAdcCtrl(codec,
          CHANNEL_RIGHT, CHANNEL_LEFT);
      break;

    case CONFIG_MIC1RP_LINE1RP_TO_RADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC1RP_LINE1RP_TO_RADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine1ToAdcCtrl(codec,
          CHANNEL_RIGHT, CHANNEL_RIGHT);
      break;

    case CONFIG_MIC1LP_LINE1LP_TO_RADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC1LP_LINE1LP_TO_RADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine1ToAdcCtrl(codec,
          CHANNEL_LEFT, CHANNEL_RIGHT);
      break;

    case CONFIG_LINE2L_TO_LADC_CTRL:
      codec->transfer.buffer[0] = REG_LINE2L_TO_LADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine2ToAdcCtrl(codec,
          CHANNEL_LEFT);
      break;
      break;

    case CONFIG_LINE2R_TO_RADC_CTRL:
      codec->transfer.buffer[0] = REG_LINE2R_TO_RADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine2ToAdcCtrl(codec,
          CHANNEL_RIGHT);
      break;

    case CONFIG_MICBIAS_CTRL:
      codec->transfer.buffer[0] = REG_MICBIAS_CTRL;
      codec->transfer.buffer[1] = makeRegMicBiasCtrl(codec);
      break;

    case CONFIG_LAGC_CTRL_ALL:
      length = makeRegAgcCtrlTransfer(codec, codec->transfer.buffer,
          CHANNEL_LEFT);
      break;

    case CONFIG_RAGC_CTRL_ALL:
      length = makeRegAgcCtrlTransfer(codec, codec->transfer.buffer,
          CHANNEL_RIGHT);
      break;

    case CONFIG_LADC_GAIN_CTRL:
      codec->transfer.buffer[0] = REG_LADC_GAIN_CTRL;
      codec->transfer.buffer[1] = makeRegAdcGainCtrl(codec, CHANNEL_LEFT);
      break;

    case CONFIG_RADC_GAIN_CTRL:
      codec->transfer.buffer[0] = REG_RADC_GAIN_CTRL;
      codec->transfer.buffer[1] = makeRegAdcGainCtrl(codec, CHANNEL_RIGHT);
      break;

    /* Input level setup */

    case CONFIG_LADC_LAGC_CTRL:
      if (codec->config.input.agc)
      {
        codec->transfer.buffer[0] = REG_LAGC_CTRL_B;
        codec->transfer.buffer[1] = makeRegAgcCtrlB(codec, CHANNEL_LEFT);
      }
      else
      {
        codec->transfer.buffer[0] = REG_LADC_GAIN_CTRL;
        codec->transfer.buffer[1] = makeRegAdcGainCtrl(codec, CHANNEL_LEFT);
      }
      break;

    case CONFIG_RADC_RAGC_CTRL:
      if (codec->config.input.agc)
      {
        codec->transfer.buffer[0] = REG_RAGC_CTRL_B;
        codec->transfer.buffer[1] = makeRegAgcCtrlB(codec, CHANNEL_RIGHT);
      }
      else
      {
        codec->transfer.buffer[0] = REG_RADC_GAIN_CTRL;
        codec->transfer.buffer[1] = makeRegAdcGainCtrl(codec, CHANNEL_RIGHT);
      }
      break;

    /* Output path setup */

    case CONFIG_HPOUT_SC:
      codec->transfer.buffer[0] = REG_HPOUT_SC;
      codec->transfer.buffer[1] = makeRegHpOutSC(codec);
      break;

    case CONFIG_HPRCOM_CFG:
      codec->transfer.buffer[0] = REG_HPRCOM_CFG;
      codec->transfer.buffer[1] = makeRegHpRCom(codec);
      break;

    case CONFIG_HPLCOM_CFG_DAC_PWR:
      codec->transfer.buffer[0] = REG_HPLCOM_CFG_DAC_PWR;
      codec->transfer.buffer[1] = makeRegHpLCom(codec);
      break;

    case CONFIG_DAC_MUX:
      codec->transfer.buffer[0] = REG_DAC_MUX;
      codec->transfer.buffer[1] = makeRegDacMux(codec);
      break;

    case CONFIG_LLOPM_CTRL:
      codec->transfer.buffer[0] = REG_LLOPM_CTRL;
      codec->transfer.buffer[1] = makeRegLOPMCtrl(codec, CHANNEL_LEFT);
      break;

    case CONFIG_RLOPM_CTRL:
      codec->transfer.buffer[0] = REG_RLOPM_CTRL;
      codec->transfer.buffer[1] = makeRegLOPMCtrl(codec, CHANNEL_RIGHT);
      break;

    case CONFIG_LDAC_VOL:
      codec->transfer.buffer[0] = REG_LDAC_VOL;
      codec->transfer.buffer[1] = makeRegDacVol(codec, CHANNEL_LEFT);
      break;

    case CONFIG_RDAC_VOL:
      codec->transfer.buffer[0] = REG_RDAC_VOL;
      codec->transfer.buffer[1] = makeRegDacVol(codec, CHANNEL_RIGHT);
      break;

    case CONFIG_DACL1_TO_HPLCOM_VOL:
      codec->transfer.buffer[0] = REG_DACL1_TO_HPLCOM_VOL;
      codec->transfer.buffer[1] = makeRegDacToHpComVol(codec, CHANNEL_LEFT);
      break;

    case CONFIG_DACR1_TO_HPRCOM_VOL:
      codec->transfer.buffer[0] = REG_DACR1_TO_HPRCOM_VOL;
      codec->transfer.buffer[1] = makeRegDacToHpComVol(codec, CHANNEL_RIGHT);
      break;

    case CONFIG_DACL1_TO_HPLOUT_VOL:
      codec->transfer.buffer[0] = REG_DACL1_TO_HPLOUT_VOL;
      codec->transfer.buffer[1] = makeRegDacToHpOutVol(codec, CHANNEL_LEFT);
      break;

    case CONFIG_DACR1_TO_HPROUT_VOL:
      codec->transfer.buffer[0] = REG_DACR1_TO_HPROUT_VOL;
      codec->transfer.buffer[1] = makeRegDacToHpOutVol(codec, CHANNEL_RIGHT);
      break;

    case CONFIG_DACL1_TO_LLOPM_VOL:
      codec->transfer.buffer[0] = REG_DACL1_TO_LLOPM_VOL;
      codec->transfer.buffer[1] = makeRegDacToLOPMVol(codec, CHANNEL_LEFT);
      break;

    case CONFIG_DACR1_TO_RLOPM_VOL:
      codec->transfer.buffer[0] = REG_DACR1_TO_RLOPM_VOL;
      codec->transfer.buffer[1] = makeRegDacToLOPMVol(codec, CHANNEL_RIGHT);
      break;

    case CONFIG_HPLCOM_CTRL:
      codec->transfer.buffer[0] = REG_HPLCOM_CTRL;
      codec->transfer.buffer[1] = makeRegHpComCtrl(codec, CHANNEL_LEFT);
      break;

    case CONFIG_HPRCOM_CTRL:
      codec->transfer.buffer[0] = REG_HPRCOM_CTRL;
      codec->transfer.buffer[1] = makeRegHpComCtrl(codec, CHANNEL_RIGHT);
      break;

    case CONFIG_HPLOUT_CTRL:
      codec->transfer.buffer[0] = REG_HPLOUT_CTRL;
      codec->transfer.buffer[1] = makeRegHpOutCtrl(codec, CHANNEL_LEFT);
      break;

    case CONFIG_HPROUT_CTRL:
      codec->transfer.buffer[0] = REG_HPROUT_CTRL;
      codec->transfer.buffer[1] = makeRegHpOutCtrl(codec, CHANNEL_RIGHT);
      break;

    /* Output level setup */

    case CONFIG_LOUT_VOL:
      length = makeOutputVolTransfer(codec, codec->transfer.buffer,
          CHANNEL_LEFT);
      break;

    case CONFIG_ROUT_VOL:
      length = makeOutputVolTransfer(codec, codec->transfer.buffer,
          CHANNEL_RIGHT);
      break;

    default:
      assert(false);
      break;
  }

  if (timeout)
  {
    codec->transfer.state = STATE_CONFIG_TIMER_WAIT;

    timerSetOverflow(codec->timer, timeout);
    timerSetValue(codec->timer, 0);
    timerEnable(codec->timer);

    return false;
  }
  else
  {
    codec->transfer.length = length;
    codec->transfer.state = STATE_CONFIG_SELECT_WAIT;

    busInit(codec);
    ifWrite(codec->bus, codec->transfer.page, sizeof(codec->transfer.page));

    return true;
  }
}
/*----------------------------------------------------------------------------*/
static void updateTask(void *argument)
{
  struct TLV320AIC3x * const codec = argument;

  codec->pending = false;
  aic3xUpdate(codec);
}
/*----------------------------------------------------------------------------*/
static enum Result aic3xInit(void *object, const void *arguments)
{
  const struct TLV320AIC3xConfig * const config = arguments;
  assert(config != NULL);
  assert(config->bus != NULL && config->timer != NULL);
  assert((config->prescaler >= 128 * 2 && config->prescaler <= 128 * 17
      && !(config->prescaler & 0xFF)) || !config->prescaler);
  assert(config->type < AIC3X_TYPE_END);

  struct TLV320AIC3x * const codec = object;
  uint8_t prescaler = config->prescaler >> 7;

  if (prescaler >= 16)
    prescaler -= 16;

  codec->reset = pinInit(config->reset);
  assert(pinValid(codec->reset));
  pinOutput(codec->reset, true);

  codec->errorCallback = NULL;
  codec->errorCallbackArgument = NULL;
  codec->idleCallback = NULL;
  codec->idleCallbackArgument = NULL;
  codec->updateCallback = NULL;
  codec->updateCallbackArgument = NULL;

  codec->bus = config->bus;
  codec->timer = config->timer;
  codec->wq = NULL;

  codec->address = config->address;
  codec->rate = config->rate;
  codec->type = config->type;
  codec->pending = false;
  codec->ready = false;
  codec->swap = config->swap;

  codec->transfer.groups = 0;
  codec->transfer.passed = 0;
  codec->transfer.state = STATE_IDLE;
  codec->transfer.step = CONFIG_END;
  codec->transfer.page[0] = REG_PAGE_SELECT;
  codec->transfer.page[1] = 0;

  codec->config.input.channels = CHANNEL_NONE;
  codec->config.input.unmute = CHANNEL_MASK;
  codec->config.input.path = AIC3X_NONE;
  codec->config.input.gainL = 0;
  codec->config.input.gainR = 0;
  codec->config.input.maxGainL = 0;
  codec->config.input.maxGainR = 0;
  codec->config.input.agcControlA = 0;
  codec->config.input.agcControlC = 0;
  codec->config.input.agc = false;

  codec->config.output.channels = CHANNEL_NONE;
  codec->config.output.unmute = CHANNEL_MASK;
  codec->config.output.path = AIC3X_NONE;
  codec->config.output.gainL = 0;
  codec->config.output.gainR = 0;

  codec->config.pll.q = prescaler;
  changeRateConfig(codec, config->samplerate);

  timerSetAutostop(codec->timer, true);
  timerSetCallback(codec->timer, onTimerEvent, codec);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void aic3xDeinit(void *object)
{
  struct TLV320AIC3x * const codec = object;

  timerDisable(codec->timer);
  timerSetCallback(codec->timer, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
void aic3xCheck(void *object)
{
  struct TLV320AIC3x * const codec = object;

  if (codec->ready)
    invokeAction(codec, GROUP_CHECK);
}
/*----------------------------------------------------------------------------*/
uint8_t aic3xGetInputGain(const void *object, enum CodecChannel channel)
{
  const struct TLV320AIC3x * const codec = object;

  switch (channel)
  {
    case CHANNEL_LEFT:
      return codec->config.input.gainL;

    case CHANNEL_RIGHT:
      return codec->config.input.gainR;

    default:
      return (codec->config.input.gainL + codec->config.input.gainR) >> 1;
  }
}
/*----------------------------------------------------------------------------*/
static enum CodecChannel aic3xGetInputMute(const void *object)
{
  const struct TLV320AIC3x * const codec = object;
  return ~codec->config.input.unmute & CHANNEL_MASK;
}
/*----------------------------------------------------------------------------*/
uint8_t aic3xGetOutputGain(const void *object, enum CodecChannel channel)
{
  const struct TLV320AIC3x * const codec = object;

  switch (channel)
  {
    case CHANNEL_LEFT:
      return codec->config.output.gainL;

    case CHANNEL_RIGHT:
      return codec->config.output.gainR;

    default:
      return (codec->config.output.gainL + codec->config.output.gainR) >> 1;
  }
}
/*----------------------------------------------------------------------------*/
static enum CodecChannel aic3xGetOutputMute(const void *object)
{
  const struct TLV320AIC3x * const codec = object;
  return ~codec->config.output.unmute & CHANNEL_MASK;
}
/*----------------------------------------------------------------------------*/
bool aic3xIsAGCEnabled(const void *object)
{
  const struct TLV320AIC3x * const codec = object;
  return codec->config.input.agc;
}
/*----------------------------------------------------------------------------*/
bool aic3xIsReady(const void *object)
{
  const struct TLV320AIC3x * const codec = object;
  return codec->ready;
}
/*----------------------------------------------------------------------------*/
void aic3xSetAGCEnabled(void *object, bool state)
{
  struct TLV320AIC3x * const codec = object;

  if (codec->config.input.agc != state)
  {
    codec->config.input.agc = state;

    if (codec->ready && codec->config.input.path != AIC3X_NONE)
      invokeAction(codec, GROUP_INPUT);
  }
}
/*----------------------------------------------------------------------------*/
void aic3xSetInputGain(void *object, enum CodecChannel channel, uint8_t gain)
{
  struct TLV320AIC3x * const codec = object;
  bool update = false;

  if ((channel == CHANNEL_LEFT) && codec->config.input.gainL != gain)
  {
    if (!codec->config.input.agc)
      codec->config.input.gainL = gain;

    codec->config.input.maxGainL = gain;
    update = true;
  }
  if ((channel == CHANNEL_RIGHT) && codec->config.input.gainR != gain)
  {
    if (!codec->config.input.agc)
      codec->config.input.gainR = gain;

    codec->config.input.maxGainR = gain;
    update = true;
  }

  if (update && codec->ready && codec->config.input.path != AIC3X_NONE)
    invokeAction(codec, GROUP_INPUT_LEVEL);
}
/*----------------------------------------------------------------------------*/
static void aic3xSetInputMute(void *object, enum CodecChannel channels)
{
  struct TLV320AIC3x * const codec = object;

  codec->config.input.unmute = ~channels & CHANNEL_MASK;

  if (codec->ready)
    invokeAction(codec, GROUP_INPUT);
}
/*----------------------------------------------------------------------------*/
void aic3xSetInputPath(void *object, int path, enum CodecChannel channels)
{
  struct TLV320AIC3x * const codec = object;

  if ((path >= AIC3X_DEFAULT_INPUT && path < AIC3X_END)
      || path == AIC3X_NONE)
  {
    if (codec->config.input.path != path
        || codec->config.input.channels != channels)
    {
      codec->config.input.channels = channels;
      codec->config.input.path = path;

      if (codec->ready)
        invokeAction(codec, GROUP_INPUT);
    }
  }
}
/*----------------------------------------------------------------------------*/
void aic3xSetOutputGain(void *object, enum CodecChannel channel, uint8_t gain)
{
  struct TLV320AIC3x * const codec = object;
  bool update = false;

  if ((channel & CHANNEL_LEFT) && codec->config.output.gainL != gain)
  {
    codec->config.output.gainL = gain;
    update = true;
  }
  if ((channel & CHANNEL_RIGHT) && codec->config.output.gainR != gain)
  {
    codec->config.output.gainR = gain;
    update = true;
  }

  if (update && codec->ready && codec->config.output.path != AIC3X_NONE)
    invokeAction(codec, GROUP_OUTPUT_LEVEL);
}
/*----------------------------------------------------------------------------*/
static void aic3xSetOutputMute(void *object, enum CodecChannel channels)
{
  struct TLV320AIC3x * const codec = object;

  codec->config.output.unmute = ~channels & CHANNEL_MASK;

  if (codec->ready && codec->config.output.path != AIC3X_NONE)
    invokeAction(codec, GROUP_OUTPUT_LEVEL);
}
/*----------------------------------------------------------------------------*/
void aic3xSetOutputPath(void *object, int path, enum CodecChannel channels)
{
  struct TLV320AIC3x * const codec = object;

  if ((path >= AIC3X_DEFAULT_OUTPUT && path < AIC3X_DEFAULT_INPUT)
      || path == AIC3X_NONE)
  {
    if (codec->config.output.path != path
        || codec->config.output.channels != channels)
    {
      codec->config.output.channels = channels;
      codec->config.output.path = path;

      if (codec->ready)
      {
        invokeAction(codec, GROUP_PATH | GROUP_OUTPUT
            | (path != AIC3X_NONE ? GROUP_OUTPUT_LEVEL : 0));
      }
    }
  }
}
/*----------------------------------------------------------------------------*/
void aic3xSetSampleRate(void *object, uint32_t rate)
{
  struct TLV320AIC3x * const codec = object;

  if (codec->config.rate != rate)
  {
    changeRateConfig(codec, rate);

    if (codec->ready)
      invokeAction(codec, GROUP_RATE | GROUP_PATH);
  }
}
/*----------------------------------------------------------------------------*/
void aic3xSetErrorCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct TLV320AIC3x * const codec = object;

  assert(callback != NULL);

  codec->errorCallbackArgument = argument;
  codec->errorCallback = callback;
}
/*----------------------------------------------------------------------------*/
void aic3xSetIdleCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct TLV320AIC3x * const codec = object;

  assert(callback != NULL);

  codec->idleCallbackArgument = argument;
  codec->idleCallback = callback;
}
/*----------------------------------------------------------------------------*/
void aic3xSetUpdateCallback(void *object, void (*callback)(void *),
    void *argument)
{
  struct TLV320AIC3x * const codec = object;

  assert(callback != NULL);
  assert(codec->wq == NULL);

  codec->updateCallbackArgument = argument;
  codec->updateCallback = callback;
}
/*----------------------------------------------------------------------------*/
void aic3xSetUpdateWorkQueue(void *object, struct WorkQueue *wq)
{
  struct TLV320AIC3x * const codec = object;

  assert(wq != NULL);
  assert(codec->updateCallback == NULL);

  codec->wq = wq;
}
/*----------------------------------------------------------------------------*/
void aic3xReset(void *object)
{
  struct TLV320AIC3x * const codec = object;

  codec->ready = false;
  codec->transfer.passed = 0;

  invokeAction(codec, GROUP_RESET | GROUP_GENERIC | GROUP_RATE | GROUP_PATH
      | GROUP_INPUT | GROUP_OUTPUT);
}
/*----------------------------------------------------------------------------*/
bool aic3xUpdate(void *object)
{
  struct TLV320AIC3x * const codec = object;
  bool busy;
  bool updated;

  do
  {
    busy = false;
    updated = false;

    switch (codec->transfer.state)
    {
      case STATE_IDLE:
        if (codec->transfer.groups)
        {
          if (codec->transfer.groups == GROUP_CHECK)
            codec->transfer.state = STATE_CHECK_START;
          else
            codec->transfer.state = STATE_CONFIG_START;
          updated = true;
        }
        break;

      case STATE_CONFIG_START:
      {
        const uint32_t mask = reverseBits32(codec->transfer.groups);

        if (mask)
        {
          const unsigned int index = countLeadingZeros32(mask);

          atomicFetchAnd(&codec->transfer.groups, (uint16_t)(~(1 << index)));
          codec->transfer.passed |= 1 << index;
          codec->transfer.step = groupIndexToConfigStep(index);
        }
        else
          codec->transfer.step = CONFIG_READY_WAIT;

        codec->transfer.state = STATE_CONFIG_UPDATE;
        updated = true;
        break;
      }

      case STATE_CONFIG_UPDATE:
        busy = startConfigUpdate(codec);
        break;

      case STATE_CONFIG_END:
        ++codec->transfer.step;

        if (isLastConfigGroupStep(codec->transfer.step))
        {
          if (codec->transfer.passed == GROUP_READY_MASK)
            codec->ready = true;
          codec->transfer.state = STATE_IDLE;

          if (!codec->transfer.groups && codec->idleCallback != NULL)
            codec->idleCallback(codec->idleCallbackArgument);
        }
        else
          codec->transfer.state = STATE_CONFIG_UPDATE;

        updated = true;
        break;

      case STATE_CHECK_START:
        atomicFetchAnd(&codec->transfer.groups, (uint16_t)(~GROUP_CHECK));
        codec->transfer.step = CHECK_GROUP_GENERIC;
        codec->transfer.state = STATE_CHECK_UPDATE;

        updated = true;
        break;

      case STATE_CHECK_UPDATE:
        updated = startCheckUpdate(codec, &busy);
        break;

      case STATE_CHECK_PROCESS:
        if (processCheckResponse(codec))
        {
          ++codec->transfer.step;
          codec->transfer.state = STATE_CHECK_UPDATE;
        }
        else
        {
          codec->ready = false;
          codec->transfer.groups = 0;
          codec->transfer.state = STATE_IDLE;

          if (codec->errorCallback != NULL)
            codec->errorCallback(codec->errorCallbackArgument);
        }

        updated = true;
        break;

      case STATE_CHECK_END:
        codec->transfer.state = STATE_IDLE;

        if (!codec->transfer.groups && codec->idleCallback != NULL)
          codec->idleCallback(codec->idleCallbackArgument);

        updated = true;
        break;

      case STATE_ERROR_INTERFACE:
      case STATE_ERROR_TIMEOUT:
        codec->transfer.groups = 0;
        codec->transfer.state = STATE_IDLE;

        if (codec->errorCallback != NULL)
          codec->errorCallback(codec->errorCallbackArgument);

        updated = true;
        break;

      default:
        break;
    }
  }
  while (updated);

  return busy;
}
/*----------------------------------------------------------------------------*/
/**
 * Configure AGC noise level of the TLV320AIC3x codec.
 * @param codec Pointer to a Codec object.
 * @param level Noise level in the range from -90 to -30 dB or 0 to disable.
 */
void aic3xSetAGCNoiseLevel(struct TLV320AIC3x *codec, int level)
{
  assert(level == 0 || (level >= -90 && level <= -30));

  if (level != 0)
  {
    codec->config.input.agcControlC =
        AGC_CTRL_C_NOISE_THRESHOLD((abs(level) - 28) / 2);
  }
  else
    codec->config.input.agcControlC = 0;

  if (codec->ready && codec->config.input.path != AIC3X_NONE)
    invokeAction(codec, GROUP_INPUT);
}
/*----------------------------------------------------------------------------*/
/**
 * Configure AGC target level of the TLV320AIC3x codec.
 * @param codec Pointer to a Codec object.
 * @param level Target level in the range from -24 to -5 dB.
 */
void aic3xSetAGCTargetLevel(struct TLV320AIC3x *codec, int level)
{
  static const int8_t levelToValueMap[] = {
      -5, -8, -10, -12, -14, -17, -20, -24
  };

  assert(level >= -24 && level <= -5);

  uint8_t value = 0;

  for (; value < ARRAY_SIZE(levelToValueMap) - 1; ++value)
  {
    if ((int)levelToValueMap[value] < level)
      break;
  }

  codec->config.input.agcControlA = AGC_CTRL_A_TARGET_LEVEL(value);

  if (codec->ready && codec->config.input.path != AIC3X_NONE)
    invokeAction(codec, GROUP_INPUT);
}
