/*
 * tlv320aic3x.c
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/audio/tlv320aic3x.h>
#include <dpm/audio/tlv320aic3x_defs.h>
#include <halm/delay.h>
#include <halm/generic/i2c.h>
#include <halm/generic/work_queue.h>
#include <halm/timer.h>
#include <xcore/accel.h>
#include <xcore/atomic.h>
#include <assert.h>
/*----------------------------------------------------------------------------*/
#define CODEC_ADDRESS 0x18
#define CODEC_RATE    400000

enum Channel
{
  CH_LEFT,
  CH_RIGHT
} __attribute__((packed));

enum ConfigGroup
{
  GROUP_RESET         = 0x01,
  GROUP_GENERIC       = 0x02,
  GROUP_RATE          = 0x04,
  GROUP_INPUT         = 0x08,
  GROUP_INPUT_LEVEL   = 0x10,
  GROUP_OUTPUT        = 0x20,
  GROUP_OUTPUT_LEVEL  = 0x40
} __attribute__((packed));

enum ConfigStep
{
  /* Hardware reset */
  CONFIG_RESET,
  CONFIG_RESET_WAIT,
  CONFIG_END_RESET,

  /* Generic config */
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
  CONFIG_CODEC_DATA_PATH_SETUP,
  CONFIG_END_GROUP_RATE,

  /* Input path setup */
  CONFIG_GROUP_INPUT,
  CONFIG_MIC2LR_LINE2LR_TO_LADC_CTRL = CONFIG_GROUP_INPUT,
  CONFIG_MIC2LR_LINE2LR_TO_RADC_CTRL,
  CONFIG_MIC1LP_LINE1LP_TO_LADC_CTRL,
  CONFIG_MIC1RP_LINE1RP_TO_RADC_CTRL,
  // CONFIG_LINE2L_TO_LADC_CTRL, // TODO
  // CONFIG_LINE2R_TO_RADC_CTRL, // TODO
  CONFIG_MICBIAS_CTRL,
  CONFIG_LAGC_CTRL_B,
  CONFIG_LAGC_CTRL_C,
  CONFIG_RAGC_CTRL_B,
  CONFIG_RAGC_CTRL_C,
  CONFIG_END_GROUP_INPUT,

  /* Input level setup */
  CONFIG_GROUP_INPUT_LEVEL,
  CONFIG_LADC_GAIN_CTRL = CONFIG_GROUP_INPUT_LEVEL,
  CONFIG_RADC_GAIN_CTRL,
  CONFIG_LAGC_CTRL_A,
  CONFIG_RAGC_CTRL_A,
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
} __attribute__((packed));

enum State
{
  STATE_IDLE,

  STATE_CONFIG_START,
  STATE_CONFIG_UPDATE,
  STATE_CONFIG_TIMER_WAIT,
  STATE_CONFIG_SELECT_WAIT,
  STATE_CONFIG_BUS_WAIT,
  STATE_CONFIG_END,

  STATE_ERROR_WAIT,
  STATE_ERROR_INTERFACE,
  STATE_ERROR_TIMEOUT
} __attribute__((packed));
/*----------------------------------------------------------------------------*/
static uint8_t convertAnalogOutputGain(uint8_t);
static uint8_t convertInputGain(uint8_t);
static inline enum ConfigStep groupIndexToConfigStep(unsigned int);
static inline bool isLastConfigGroupStep(enum ConfigStep);
static uint8_t makeRegAdcGainCtrl(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegAgcCtrlA(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegAgcCtrlB(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegASDIfCtrlA(const struct TLV320AIC3x *);
static uint8_t makeRegASDIfCtrlB(const struct TLV320AIC3x *);
static uint8_t makeRegCodecDataPathSetup(const struct TLV320AIC3x *);
static uint8_t makeRegDacMux(const struct TLV320AIC3x *);
static uint8_t makeRegDacToHpComVol(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegDacToHpOutVol(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegDacToLOPMVol(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegDacVol(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegHpLCom(const struct TLV320AIC3x *);
static uint8_t makeRegHpRCom(const struct TLV320AIC3x *);
static uint8_t makeRegLOPMCtrl(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegHpComCtrl(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegHpOutCtrl(const struct TLV320AIC3x *, enum Channel);
static uint8_t makeRegHpOutSC(const struct TLV320AIC3x *);
static uint8_t makeRegMicBiasCtrl(const struct TLV320AIC3x *);
static uint8_t makeRegMicLine1ToAdcCtrl(const struct TLV320AIC3x *,
    enum Channel);
static uint8_t makeRegMicLine2ToAdcCtrl(const struct TLV320AIC3x *,
    enum Channel);

static void makeOutputVolTransfer(const struct TLV320AIC3x *, uint8_t *,
    enum Channel);

static void busInit(struct TLV320AIC3x *, bool);
static void changeRateConfig(struct TLV320AIC3x *, unsigned int);
static void invokeAction(struct TLV320AIC3x *, uint8_t);
static void invokeUpdate(struct TLV320AIC3x *);
static void onBusEvent(void *);
static void onTimerEvent(void *);
static bool startConfigUpdate(struct TLV320AIC3x *);
static void updateTask(void *);
/*----------------------------------------------------------------------------*/
static enum Result codecInit(void *, const void *);
static void codecDeinit(void *);
/*----------------------------------------------------------------------------*/
const struct EntityClass * const TLV320AIC3x =
    &(const struct EntityClass){
    .size = sizeof(struct TLV320AIC3x),
    .init = codecInit,
    .deinit = codecDeinit
};
/*----------------------------------------------------------------------------*/
static uint8_t convertAnalogOutputGain(uint8_t gain)
{
  return (255 - gain) * 118 / 256;
}
/*----------------------------------------------------------------------------*/
static uint8_t convertInputGain(uint8_t gain)
{
  return (gain * 120) / 256;
}
/*----------------------------------------------------------------------------*/
static inline enum ConfigStep groupIndexToConfigStep(unsigned int index)
{
  static const enum ConfigStep INDEX_TO_GROUP_MAP[] = {
      CONFIG_RESET,
      CONFIG_GROUP_GENERIC,
      CONFIG_GROUP_RATE,
      CONFIG_GROUP_INPUT,
      CONFIG_GROUP_INPUT_LEVEL,
      CONFIG_GROUP_OUTPUT,
      CONFIG_GROUP_OUTPUT_LEVEL
  };

  assert(index < ARRAY_SIZE(INDEX_TO_GROUP_MAP));
  return INDEX_TO_GROUP_MAP[index];
}
/*----------------------------------------------------------------------------*/
static inline bool isLastConfigGroupStep(enum ConfigStep step)
{
  return step == CONFIG_END
     || step == CONFIG_END_RESET
     || step == CONFIG_END_GROUP_GENERIC
     || step == CONFIG_END_GROUP_RATE
     || step == CONFIG_END_GROUP_INPUT
     || step == CONFIG_END_GROUP_INPUT_LEVEL
     || step == CONFIG_END_GROUP_OUTPUT
     || step == CONFIG_END_GROUP_OUTPUT_LEVEL;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegAdcGainCtrl(const struct TLV320AIC3x *codec,
    enum Channel channel __attribute__((unused)))
{
  if (codec->config.input.path != AIC3X_NONE)
  {
    const uint8_t gain = convertInputGain(channel == CH_LEFT ?
        codec->config.input.gainL : codec->config.input.gainR);

    return ADC_PGA_GAIN(gain);
  }
  else
    return ADC_PGA_MUTE;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegAgcCtrlA(const struct TLV320AIC3x *codec,
    enum Channel channel __attribute__((unused)))
{
  if (codec->config.input.path != AIC3X_NONE)
  {
    if (codec->config.input.agc)
      return AGC_CTRL_A_ENABLE;
  }

  return 0;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegAgcCtrlB(const struct TLV320AIC3x *codec
    __attribute__((unused)), enum Channel channel __attribute__((unused)))
{
  return AGC_CTRL_B_MAX_GAIN(AGC_CTRL_B_MAX_GAIN_MAX);
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegASDIfCtrlA(const struct TLV320AIC3x *codec
    __attribute__((unused)))
{
  return ASDA_DOUT_3_STATE_CONTROL;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegASDIfCtrlB(const struct TLV320AIC3x *codec
    __attribute__((unused)))
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
    value |= DATA_PATH_SETUP_LDAC(DAC_PATH_LEFT)
        | DATA_PATH_SETUP_RDAC(DAC_PATH_RIGHT);
  }

  if (codec->config.rate == 96000 || codec->config.rate == 88200)
    value |= DATA_PATH_SETUP_DAC_DUAL_RATE | DATA_PATH_SETUP_ADC_DUAL_RATE;

  if (codec->config.rate == 96000 || codec->config.rate == 48000)
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
    enum Channel channel)
{
  switch (codec->config.output.path)
  {
    case AIC3X_HP_COM:
    {
      const uint8_t gain = convertAnalogOutputGain(channel == CH_LEFT ?
          codec->config.output.gainL : codec->config.output.gainR);

      return DAC_PGA_ANALOG_VOL_GAIN(gain) | DAC_PGA_ANALOG_VOL_UNMUTE;
    }

    default:
      return 0;
  }
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegDacToHpOutVol(const struct TLV320AIC3x *codec,
    enum Channel channel)
{
  switch (codec->config.output.path)
  {
    case AIC3X_HP_OUT:
    case AIC3X_HP_OUT_DIFF:
    {
      const uint8_t gain = convertAnalogOutputGain(channel == CH_LEFT ?
          codec->config.output.gainL : codec->config.output.gainR);

      return DAC_PGA_ANALOG_VOL_GAIN(gain) | DAC_PGA_ANALOG_VOL_UNMUTE;
    }

    default:
      return 0;
  }
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegDacToLOPMVol(const struct TLV320AIC3x *codec,
    enum Channel channel)
{
  switch (codec->config.output.path)
  {
    case AIC3X_LINE_OUT:
    case AIC3X_LINE_OUT_DIFF:
    {
      const uint8_t gain = convertAnalogOutputGain(channel == CH_LEFT ?
          codec->config.output.gainL : codec->config.output.gainR);

      return DAC_PGA_ANALOG_VOL_GAIN(gain) | DAC_PGA_ANALOG_VOL_UNMUTE;
    }

    default:
      return 0;
  }
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegDacVol(const struct TLV320AIC3x *codec,
    enum Channel channel __attribute__((unused)))
{
  if (codec->config.output.path != AIC3X_NONE)
    return DAC_DIGITAL_VOL_GAIN(0);
  else
    return DAC_DIGITAL_VOL_MUTE;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpLCom(const struct TLV320AIC3x *codec)
{
  uint8_t value = 0;

  if (codec->config.output.path != AIC3X_NONE)
    value |= HPLCOM_RDAC_POWER_CONTROL | HPLCOM_LDAC_POWER_CONTROL;

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

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpRCom(const struct TLV320AIC3x *codec)
{
  uint8_t value = HPRCOM_CFG_SC_LIMIT | HPRCOM_CFG_SC_ENABLE;

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

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegLOPMCtrl(const struct TLV320AIC3x *codec,
    enum Channel channel __attribute__((unused)))
{
  switch (codec->config.output.path)
  {
    case AIC3X_LINE_OUT:
    case AIC3X_LINE_OUT_DIFF:
      /* 0 dB output level */
      return OUTPUT_POWER_CONTROL | OUTPUT_UNMUTE | OUTPUT_GAIN(0);

    default:
      return 0;
  }
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpComCtrl(const struct TLV320AIC3x *codec,
    enum Channel channel __attribute__((unused)))
{
  switch (codec->config.output.path)
  {
    case AIC3X_HP_COM:
      /* 9 dB output level */
      return OUTPUT_POWER_CONTROL | OUTPUT_UNMUTE | OUTPUT_GAIN(9);

    default:
      return 0;
  }
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegHpOutCtrl(const struct TLV320AIC3x *codec,
    enum Channel channel __attribute__((unused)))
{
  switch (codec->config.output.path)
  {
    case AIC3X_HP_OUT:
    case AIC3X_HP_OUT_DIFF:
      /* 9 dB output level */
      return OUTPUT_POWER_CONTROL | OUTPUT_UNMUTE | OUTPUT_GAIN(9);

    default:
      return 0;
  }
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
    enum Channel channel __attribute__((unused)))
{
  uint8_t value = MIC_LINE_LP_RP_SOFT_STEPPING(MIC_LINE_SOFT_STEPPING_DISABLED);

  if (codec->config.input.path != AIC3X_NONE)
    value |= MIC_LINE_LP_RP_ENABLE;

  switch (codec->config.input.path)
  {
    case AIC3X_MIC_1_IN_DIFF:
    case AIC3X_LINE_1_IN_DIFF:
      value |= MIC_LINE_LP_RP_GAIN(0) | MIC_LINE_LP_RP_DIFF;
      break;

    case AIC3X_MIC_1_IN:
    case AIC3X_LINE_1_IN:
      value |= MIC_LINE_LP_RP_GAIN(0);
      break;

    default:
      value |= MIC_LINE_LP_RP_GAIN(MIC_LINE_GAIN_DISABLED);
      break;
  }

  return value;
}
/*----------------------------------------------------------------------------*/
static uint8_t makeRegMicLine2ToAdcCtrl(const struct TLV320AIC3x *codec,
    enum Channel channel)
{
  switch (codec->config.input.path)
  {
    case AIC3X_MIC_2_IN:
    case AIC3X_LINE_2_IN:
      if (channel == CH_LEFT)
        return MIC_LINE_R_GAIN(MIC_LINE_GAIN_DISABLED) | MIC_LINE_L_GAIN(0);
      else
        return MIC_LINE_R_GAIN(0) | MIC_LINE_L_GAIN(MIC_LINE_GAIN_DISABLED);

    default:
      return MIC_LINE_R_GAIN(MIC_LINE_GAIN_DISABLED)
          | MIC_LINE_L_GAIN(MIC_LINE_GAIN_DISABLED);
  }
}
/*----------------------------------------------------------------------------*/
static void makeOutputVolTransfer(const struct TLV320AIC3x *codec,
    uint8_t *buffer, enum Channel channel)
{
  assert(codec->config.output.path != AIC3X_NONE);

  const uint8_t gain = convertAnalogOutputGain(channel == CH_LEFT ?
      codec->config.output.gainL : codec->config.output.gainR);

  switch (codec->config.output.path)
  {
    case AIC3X_HP_COM:
      buffer[0] = channel == CH_LEFT ?
          REG_DACL1_TO_HPLCOM_VOL : REG_DACR1_TO_HPRCOM_VOL;
      break;

    case AIC3X_HP_OUT:
    case AIC3X_HP_OUT_DIFF:
      buffer[0] = channel == CH_LEFT ?
          REG_DACL1_TO_HPLOUT_VOL : REG_DACR1_TO_HPROUT_VOL;
      break;

    default:
      buffer[0] = channel == CH_LEFT ?
          REG_DACL1_TO_LLOPM_VOL : REG_DACL1_TO_RLOPM_VOL;
      break;
  }

  buffer[1] = DAC_PGA_ANALOG_VOL_GAIN(gain) | DAC_PGA_ANALOG_VOL_UNMUTE;
}
/*----------------------------------------------------------------------------*/
static void busInit(struct TLV320AIC3x *codec, bool read)
{
  /* Lock the interface */
  ifSetParam(codec->bus, IF_ACQUIRE, NULL);

  ifSetParam(codec->bus, IF_ZEROCOPY, NULL);
  ifSetCallback(codec->bus, onBusEvent, codec);

  if (codec->rate)
    ifSetParam(codec->bus, IF_RATE, &codec->rate);

  ifSetParam(codec->bus, IF_ADDRESS, &codec->address);

  if (read)
    ifSetParam(codec->bus, IF_I2C_REPEATED_START, NULL);

  /* Start bus watchdog */
  timerSetOverflow(codec->timer, timerGetFrequency(codec->timer) / 10);
  timerSetValue(codec->timer, 0);
  timerEnable(codec->timer);
}
/*----------------------------------------------------------------------------*/
static void changeRateConfig(struct TLV320AIC3x *codec, unsigned int rate)
{
  codec->config.rate = rate;

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
static void invokeAction(struct TLV320AIC3x *codec, uint8_t actions)
{
  const uint8_t previous = atomicFetchOr(&codec->transfer.groups, actions);

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
    timerSetOverflow(codec->timer, timerGetFrequency(codec->timer) / 10);
    timerSetValue(codec->timer, 0);
    timerEnable(codec->timer);
  }

  switch (codec->transfer.state)
  {
    case STATE_CONFIG_SELECT_WAIT:
      busy = true;
      codec->transfer.state = STATE_CONFIG_BUS_WAIT;

      ifWrite(codec->bus, codec->transfer.buffer, codec->transfer.length);
      break;

    case STATE_CONFIG_BUS_WAIT:
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
static bool startConfigUpdate(struct TLV320AIC3x *codec)
{
  size_t length = 2;
  uint32_t timeout = 0;

  switch (codec->transfer.step)
  {
    case CONFIG_RESET:
      pinReset(codec->reset);
      timeout = timerGetFrequency(codec->timer) / 100;
      break;

    case CONFIG_RESET_WAIT:
      pinSet(codec->reset);
      timeout = timerGetFrequency(codec->timer) / 100;
      break;

    case CONFIG_READY_WAIT:
      timeout = timerGetFrequency(codec->timer) / 100;
      break;

    /* Generic config */

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
      codec->transfer.buffer[1] =
          PLLA_P(codec->config.pll.p) | PLLA_Q(2) | PLLA_ENABLE;
      break;

    case CONFIG_CODEC_DATA_PATH_SETUP:
      codec->transfer.buffer[0] = REG_CODEC_DATA_PATH_SETUP;
      codec->transfer.buffer[1] = makeRegCodecDataPathSetup(codec);
      break;

    /* Input path setup */

    case CONFIG_MIC2LR_LINE2LR_TO_LADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC2LR_LINE2LR_TO_LADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine2ToAdcCtrl(codec, CH_LEFT);
      break;

    case CONFIG_MIC2LR_LINE2LR_TO_RADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC2LR_LINE2LR_TO_RADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine2ToAdcCtrl(codec, CH_RIGHT);
      break;

    case CONFIG_MIC1LP_LINE1LP_TO_LADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC1LP_LINE1LP_TO_LADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine1ToAdcCtrl(codec, CH_LEFT);
      break;

    case CONFIG_MIC1RP_LINE1RP_TO_RADC_CTRL:
      codec->transfer.buffer[0] = REG_MIC1RP_LINE1RP_TO_RADC_CTRL;
      codec->transfer.buffer[1] = makeRegMicLine1ToAdcCtrl(codec, CH_RIGHT);
      break;

    // case CONFIG_LINE2L_TO_LADC_CTRL:
    //   break;

    // case CONFIG_LINE2R_TO_RADC_CTRL:
    //   break;

    case CONFIG_MICBIAS_CTRL:
      codec->transfer.buffer[0] = REG_MICBIAS_CTRL;
      codec->transfer.buffer[1] = makeRegMicBiasCtrl(codec);
      break;

    case CONFIG_LAGC_CTRL_B:
      codec->transfer.buffer[0] = REG_LAGC_CTRL_B;
      codec->transfer.buffer[1] = makeRegAgcCtrlB(codec, CH_LEFT);
      break;

    case CONFIG_LAGC_CTRL_C:
      codec->transfer.buffer[0] = REG_LAGC_CTRL_C;
      codec->transfer.buffer[1] = 0;
      break;

    case CONFIG_RAGC_CTRL_B:
      codec->transfer.buffer[0] = REG_RAGC_CTRL_B;
      codec->transfer.buffer[1] = makeRegAgcCtrlB(codec, CH_RIGHT);
      break;

    case CONFIG_RAGC_CTRL_C:
      codec->transfer.buffer[0] = REG_RAGC_CTRL_C;
      codec->transfer.buffer[1] = 0;
      break;

    /* Input level setup */

    case CONFIG_LADC_GAIN_CTRL:
      codec->transfer.buffer[0] = REG_LADC_GAIN_CTRL;
      codec->transfer.buffer[1] = makeRegAdcGainCtrl(codec, CH_LEFT);
      break;

    case CONFIG_RADC_GAIN_CTRL:
      codec->transfer.buffer[0] = REG_LADC_GAIN_CTRL;
      codec->transfer.buffer[1] = makeRegAdcGainCtrl(codec, CH_RIGHT);
      break;

    case CONFIG_LAGC_CTRL_A:
      codec->transfer.buffer[0] = REG_LAGC_CTRL_A;
      codec->transfer.buffer[1] = makeRegAgcCtrlA(codec, CH_LEFT);
      break;

    case CONFIG_RAGC_CTRL_A:
      codec->transfer.buffer[0] = REG_RAGC_CTRL_A;
      codec->transfer.buffer[1] = makeRegAgcCtrlA(codec, CH_RIGHT);
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
      codec->transfer.buffer[1] = makeRegLOPMCtrl(codec, CH_LEFT);
      break;

    case CONFIG_RLOPM_CTRL:
      codec->transfer.buffer[0] = REG_RLOPM_CTRL;
      codec->transfer.buffer[1] = makeRegLOPMCtrl(codec, CH_RIGHT);
      break;

    case CONFIG_LDAC_VOL:
      codec->transfer.buffer[0] = REG_LDAC_VOL;
      codec->transfer.buffer[1] = makeRegDacVol(codec, CH_LEFT);
      break;

    case CONFIG_RDAC_VOL:
      codec->transfer.buffer[0] = REG_RDAC_VOL;
      codec->transfer.buffer[1] = makeRegDacVol(codec, CH_RIGHT);
      break;

    case CONFIG_DACL1_TO_HPLCOM_VOL:
      codec->transfer.buffer[0] = REG_DACL1_TO_HPLCOM_VOL;
      codec->transfer.buffer[1] = makeRegDacToHpComVol(codec, CH_LEFT);
      break;

    case CONFIG_DACR1_TO_HPRCOM_VOL:
      codec->transfer.buffer[0] = REG_DACR1_TO_HPRCOM_VOL;
      codec->transfer.buffer[1] = makeRegDacToHpComVol(codec, CH_RIGHT);
      break;

    case CONFIG_DACL1_TO_HPLOUT_VOL:
      codec->transfer.buffer[0] = REG_DACL1_TO_HPLOUT_VOL;
      codec->transfer.buffer[1] = makeRegDacToHpOutVol(codec, CH_LEFT);
      break;

    case CONFIG_DACR1_TO_HPROUT_VOL:
      codec->transfer.buffer[0] = REG_DACR1_TO_HPROUT_VOL;
      codec->transfer.buffer[1] = makeRegDacToHpOutVol(codec, CH_RIGHT);
      break;

    case CONFIG_DACL1_TO_LLOPM_VOL:
      codec->transfer.buffer[0] = REG_DACL1_TO_LLOPM_VOL;
      codec->transfer.buffer[1] = makeRegDacToLOPMVol(codec, CH_LEFT);
      break;

    case CONFIG_DACR1_TO_RLOPM_VOL:
      codec->transfer.buffer[0] = REG_DACR1_TO_RLOPM_VOL;
      codec->transfer.buffer[1] = makeRegDacToLOPMVol(codec, CH_RIGHT);
      break;

    case CONFIG_HPLCOM_CTRL:
      codec->transfer.buffer[0] = REG_HPLCOM_CTRL;
      codec->transfer.buffer[1] = makeRegHpComCtrl(codec, CH_LEFT);
      break;

    case CONFIG_HPRCOM_CTRL:
      codec->transfer.buffer[0] = REG_HPRCOM_CTRL;
      codec->transfer.buffer[1] = makeRegHpComCtrl(codec, CH_RIGHT);
      break;

    case CONFIG_HPLOUT_CTRL:
      codec->transfer.buffer[0] = REG_HPLOUT_CTRL;
      codec->transfer.buffer[1] = makeRegHpOutCtrl(codec, CH_LEFT);
      break;

    case CONFIG_HPROUT_CTRL:
      codec->transfer.buffer[0] = REG_HPROUT_CTRL;
      codec->transfer.buffer[1] = makeRegHpOutCtrl(codec, CH_RIGHT);
      break;

    /* Output level setup */

    case CONFIG_LOUT_VOL:
      makeOutputVolTransfer(codec, codec->transfer.buffer, CH_LEFT);
      break;

    case CONFIG_ROUT_VOL:
      makeOutputVolTransfer(codec, codec->transfer.buffer, CH_RIGHT);
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

    busInit(codec, false);
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
static enum Result codecInit(void *object, const void *arguments)
{
  const struct TLV320AIC3xConfig * const config = arguments;
  assert(config != NULL);
  assert(config->bus != NULL && config->timer != NULL);

  struct TLV320AIC3x * const codec = object;

  codec->reset = pinInit(config->reset);
  assert(pinValid(codec->reset));
  pinOutput(codec->reset, true);

  codec->bus = config->bus;
  codec->timer = config->timer;
  codec->wq = NULL;

  codec->errorCallback = NULL;
  codec->errorCallbackArgument = NULL;
  codec->updateCallback = NULL;
  codec->updateCallbackArgument = NULL;

  codec->address = config->address ? config->address : CODEC_ADDRESS;
  codec->rate = config->rate ? config->rate : CODEC_RATE;
  codec->pending = false;

  codec->transfer.groups = 0;
  codec->transfer.state = STATE_IDLE;
  codec->transfer.step = CONFIG_END;
  codec->transfer.page[0] = REG_PAGE_SELECT;
  codec->transfer.page[1] = 0;

  timerSetAutostop(codec->timer, true);
  timerSetCallback(codec->timer, onTimerEvent, codec);

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void codecDeinit(void *object)
{
  struct TLV320AIC3x * const codec = object;

  timerDisable(codec->timer);
  timerSetCallback(codec->timer, NULL, NULL);
}
/*----------------------------------------------------------------------------*/
int aic3xGetInputGain(const struct TLV320AIC3x *codec)
{
  return (codec->config.input.gainL + codec->config.input.gainR) >> 1;
}
/*----------------------------------------------------------------------------*/
int aic3xGetOutputGain(const struct TLV320AIC3x *codec)
{
  return (codec->config.output.gainL + codec->config.output.gainR) >> 1;
}
/*----------------------------------------------------------------------------*/
void aic3xReset(struct TLV320AIC3x *codec, uint32_t rate,
    enum AIC3xPath inputPath, uint8_t inputGain, bool inputAutoGain,
    enum AIC3xPath outputPath, uint8_t outputGain)
{
  changeRateConfig(codec, rate);

  codec->config.input.path = inputPath;
  codec->config.input.gainL = inputGain;
  codec->config.input.gainR = inputGain;
  codec->config.input.agc = inputAutoGain;
  codec->config.output.path = outputPath;
  codec->config.output.gainL = outputGain;
  codec->config.output.gainR = outputGain;

  invokeAction(codec, GROUP_RESET | GROUP_GENERIC | GROUP_RATE
      | GROUP_INPUT | GROUP_INPUT_LEVEL | GROUP_OUTPUT);
}
/*----------------------------------------------------------------------------*/
void aic3xSetInputGain(struct TLV320AIC3x *codec, uint8_t gain, bool agc)
{
  codec->config.input.gainL = gain;
  codec->config.input.gainR = gain;
  codec->config.input.agc = agc;

  if (codec->config.input.path != AIC3X_NONE)
    invokeAction(codec, GROUP_INPUT_LEVEL);
}
/*----------------------------------------------------------------------------*/
void aic3xSetInputGainLR(struct TLV320AIC3x *codec, uint8_t gainL,
    uint8_t gainR, bool agc)
{
  codec->config.input.gainL = gainL;
  codec->config.input.gainR = gainR;
  codec->config.input.agc = agc;

  if (codec->config.input.path != AIC3X_NONE)
    invokeAction(codec, GROUP_INPUT_LEVEL);
}
/*----------------------------------------------------------------------------*/
void aic3xSetInputPath(struct TLV320AIC3x *codec, enum AIC3xPath path)
{
  assert(path >= AIC3X_DEFAULT_INPUT && path < AIC3X_END);
  codec->config.input.path = path;

  invokeAction(codec, GROUP_INPUT | GROUP_INPUT_LEVEL);
}
/*----------------------------------------------------------------------------*/
void aic3xSetOutputGain(struct TLV320AIC3x *codec, uint8_t gain)
{
  codec->config.output.gainL = gain;
  codec->config.output.gainR = gain;

  if (codec->config.output.path != AIC3X_NONE)
    invokeAction(codec, GROUP_OUTPUT_LEVEL);
}
/*----------------------------------------------------------------------------*/
void aic3xSetOutputGainLR(struct TLV320AIC3x *codec, uint8_t gainL,
    uint8_t gainR)
{
  codec->config.output.gainL = gainL;
  codec->config.output.gainR = gainR;

  if (codec->config.output.path != AIC3X_NONE)
    invokeAction(codec, GROUP_OUTPUT_LEVEL);
}
/*----------------------------------------------------------------------------*/
void aic3xSetOutputPath(struct TLV320AIC3x *codec, enum AIC3xPath path)
{
  assert(path >= AIC3X_DEFAULT_OUTPUT && path < AIC3X_DEFAULT_INPUT);
  codec->config.output.path = path;

  invokeAction(codec, GROUP_OUTPUT);
}
/*----------------------------------------------------------------------------*/
void aic3xSetRate(struct TLV320AIC3x *codec, uint32_t rate)
{
  if (codec->config.rate != rate)
  {
    changeRateConfig(codec, rate);
    invokeAction(codec, GROUP_RATE);
  }
}
/*----------------------------------------------------------------------------*/
void aic3xSetErrorCallback(struct TLV320AIC3x *codec, void (*callback)(void *),
    void *argument)
{
  assert(callback != NULL);

  codec->errorCallbackArgument = argument;
  codec->errorCallback = callback;
}
/*----------------------------------------------------------------------------*/
void aic3xSetUpdateCallback(struct TLV320AIC3x *codec, void (*callback)(void *),
    void *argument)
{
  assert(callback != NULL);
  assert(codec->wq == NULL);

  codec->updateCallbackArgument = argument;
  codec->updateCallback = callback;
}
/*----------------------------------------------------------------------------*/
void aic3xSetUpdateWorkQueue(struct TLV320AIC3x *codec, struct WorkQueue *wq)
{
  assert(wq != NULL);
  assert(codec->updateCallback == NULL);

  codec->wq = wq;
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

          atomicFetchAnd(&codec->transfer.groups, ~(1 << index));
          codec->transfer.step = groupIndexToConfigStep(index);
        }
        else
        {
          codec->transfer.step = CONFIG_READY_WAIT;
        }

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
          codec->transfer.state = STATE_IDLE;
        else
          codec->transfer.state = STATE_CONFIG_UPDATE;

        updated = true;
        break;

      case STATE_ERROR_INTERFACE:
      case STATE_ERROR_TIMEOUT:
        if (codec->errorCallback != NULL)
          codec->errorCallback(codec->errorCallbackArgument);

        codec->transfer.groups = 0;
        codec->transfer.state = STATE_IDLE;
        break;

      default:
        break;
    }
  }
  while (updated);

  return busy;
}
