/*
 * audio/codec.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_AUDIO_CODEC_H_
#define DPM_AUDIO_CODEC_H_
/*----------------------------------------------------------------------------*/
#include <xcore/entity.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
struct WorkQueue;

enum [[gnu::packed]] CodecChannel
{
  CHANNEL_NONE  = 0x00,
  CHANNEL_LEFT  = 0x01,
  CHANNEL_RIGHT = 0x02
};

/* Class descriptor */
struct CodecClass
{
  CLASS_HEADER

  /* Getters */
  uint8_t (*getInputGain)(const void *, enum CodecChannel);
  enum CodecChannel (*getInputMute)(const void *);
  uint8_t (*getOutputGain)(const void *, enum CodecChannel);
  enum CodecChannel (*getOutputMute)(const void *);
  bool (*isAGCEnabled)(const void *);
  bool (*isReady)(const void *);

  /* Setters */
  void (*setAGCEnabled)(void *, bool);
  void (*setInputGain)(void *, enum CodecChannel, uint8_t);
  void (*setInputMute)(void *, enum CodecChannel);
  void (*setInputPath)(void *, int, enum CodecChannel);
  void (*setOutputGain)(void *, enum CodecChannel, uint8_t);
  void (*setOutputMute)(void *, enum CodecChannel);
  void (*setOutputPath)(void *, int, enum CodecChannel);
  void (*setSampleRate)(void *, uint32_t);

  void (*setErrorCallback)(void *, void (*)(void *), void *);
  void (*setIdleCallback)(void *, void (*)(void *), void *);
  void (*setUpdateCallback)(void *, void (*)(void *), void *);
  void (*setUpdateWorkQueue)(void *, struct WorkQueue *);

  void (*check)(void *);
  void (*reset)(void *);
  void (*suspend)(void *);
  bool (*update)(void *);
};

struct Codec
{
  struct Entity base;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

/**
 * Asynchronously check the codec status.
 * @param codec Pointer to a Codec object.
 */
static inline void codecCheck(void *codec)
{
  ((const struct CodecClass *)CLASS(codec))->check(codec);
}

/**
 * Get the current input gain of a selected channel.
 *
 * The gain value is normalized to an 8‑bit range.
 *
 * @param codec Pointer to a Codec object.
 * @param channel Input channel identifier.
 * @return Input gain in the range from 0 to 255, where:
 *   - 0 means the channel is muted;
 *   - 255 represents maximum gain.
 */
static inline uint8_t codecGetInputGain(const void *codec,
    enum CodecChannel channel)
{
  return ((const struct CodecClass *)CLASS(codec))->getInputGain(codec,
      channel);
}

/**
 * Get the current mute status of input channels.
 *
 * @param codec Pointer to a Codec object.
 * @return Bitmask of muted input channels (each bit corresponds to a channel).
 * A set bit indicates that the respective channel is muted.
 */
static inline enum CodecChannel codecGetInputMute(const void *codec)
{
  return ((const struct CodecClass *)CLASS(codec))->getInputMute(codec);
}

/**
 * Get the output gain of a selected channel.
 *
 * The gain value is normalized to an 8‑bit range.
 *
 * @param codec Pointer to a Codec object.
 * @param channel Output channel identifier.
 * @return Output gain in the range from 0 to 255, where:
 *   - 0 means the channel is muted;
 *   - 255 represents maximum gain.
 */
static inline uint8_t codecGetOutputGain(const void *codec,
    enum CodecChannel channel)
{
  return ((const struct CodecClass *)CLASS(codec))->getOutputGain(codec,
      channel);
}

/**
 * Get the current mute status of output channels.
 *
 * @param codec Pointer to a Codec object.
 * @return Bitmask of muted output channels (each bit corresponds to a channel).
 * A set bit indicates that the respective channel is muted.
 */
static inline enum CodecChannel codecGetOutputMute(const void *codec)
{
  return ((const struct CodecClass *)CLASS(codec))->getOutputMute(codec);
}

/**
 * Check whether automatic gain control (AGC) is enabled for an input channel.
 *
 * @param codec Pointer to a Codec object.
 * @return AGC status:
 *   - @b true if AGC is enabled;
 *   - @b false if AGC is disabled.
 */
static inline bool codecIsAGCEnabled(const void *codec)
{
  return ((const struct CodecClass *)CLASS(codec))->isAGCEnabled(codec);
}

/**
 * Get the overall codec status.
 *
 * @param codec Pointer to a Codec object.
 * @return Codec status:
 *   - @b true if the codec is ready and operational;
 *   - @b false if the codec is not ready.
 */
static inline bool codecIsReady(const void *codec)
{
  return ((const struct CodecClass *)CLASS(codec))->isReady(codec);
}

/**
 * Enable or disable automatic gain control (AGC) for an input channel.
 *
 * @param codec Pointer to a Codec object.
 * @param state AGC state:
 *   - @b true to enable AGC;
 *   - @b false to disable AGC.
 */
static inline void codecSetAGCEnabled(void *codec, bool state)
{
  ((const struct CodecClass *)CLASS(codec))->setAGCEnabled(codec, state);
}

/**
 * Set the input gain for a selected channel.
 *
 * Gain is specified as an 8‑bit value for fine control.
 *
 * @param codec Pointer to a Codec object.
 * @param channel Input channel identifier.
 * @param gain Input gain value in the range from 0 to 255:
 *   - 0: channel is muted;
 *   - 255: maximum gain (full volume).
 */
static inline void codecSetInputGain(void *codec, enum CodecChannel channel,
    uint8_t gain)
{
  ((const struct CodecClass *)CLASS(codec))->setInputGain(codec, channel, gain);
}

/**
 * Mute or unmute specific input channels.
 *
 * Uses a bitmask to control multiple channels simultaneously.
 *
 * @param codec Pointer to a Codec object.
 * @param channels Bitmask of input channels to mute/unmute.
 * Set bits correspond to channels to be muted.
 */
static inline void codecSetInputMute(void *codec, enum CodecChannel channels)
{
  ((const struct CodecClass *)CLASS(codec))->setInputMute(codec, channels);
}

/**
 * Configure the input path and assign input channels.
 *
 * Defines the signal routing for input sources.
 *
 * @param codec Pointer to a Codec object.
 * @param path Input path identifier.
 * @param channels Bitmask specifying which input channels are active
 * on this path.
 */
static inline void codecSetInputPath(void *codec, int path,
    enum CodecChannel channels)
{
  ((const struct CodecClass *)CLASS(codec))->setInputPath(codec, path,
      channels);
}

/**
 * Set the output gain for a selected channel.
 *
 * Gain is specified as an 8‑bit value for precise control.
 *
 * @param codec Pointer to a Codec object.
 * @param channel Output channel identifier.
 * @param gain Output gain value in the range from 0 to 255:
 *   - 0: channel is muted;
 *   - 255: maximum gain (full volume).
 */
static inline void codecSetOutputGain(void *codec, enum CodecChannel channel,
    uint8_t gain)
{
  ((const struct CodecClass *)CLASS(codec))->setOutputGain(codec, channel,
      gain);
}

/**
 * Mute or unmute specific output channels.
 *
 * Uses a bitmask to control multiple channels at once.
 *
 * @param codec Pointer to a Codec object.
 * @param channels Bitmask of output channels to mute/unmute.
 * Set bits correspond to channels to be muted.
 */
static inline void codecSetOutputMute(void *codec, enum CodecChannel channels)
{
  ((const struct CodecClass *)CLASS(codec))->setOutputMute(codec, channels);
}

/**
 * Set output path and output channels.
 *
 * @param codec Pointer to a Codec object.
 * @param path Output path identifier.
 * @param channels Output channels to be configured.
 */
static inline void codecSetOutputPath(void *codec, int path,
    enum CodecChannel channels)
{
  ((const struct CodecClass *)CLASS(codec))->setOutputPath(codec, path,
      channels);
}

/**
 * Set sample rate for all input and output channels.
 *
 * @param codec Pointer to a Codec object.
 * @param rate Sample rate in Hz.
 */
static inline void codecSetSampleRate(void *codec, uint32_t rate)
{
  ((const struct CodecClass *)CLASS(codec))->setSampleRate(codec, rate);
}

/**
 * Reset the codec.
 *
 * Perform a software or hardware reset and reconfigure the codec.
 * All previously configured parameters will be preserved after the reset.
 *
 * @param codec Pointer to a Codec object.
 */
static inline void codecReset(void *codec)
{
  ((const struct CodecClass *)CLASS(codec))->reset(codec);
}

/**
 * Put the codec in a power‑saving mode.
 *
 * Suspends the codec operation to reduce power consumption.
 * The codec may need to be reinitialized when resuming.
 *
 * @param codec Pointer to a Codec object.
 */
static inline void codecSuspend(void *codec)
{
  ((const struct CodecClass *)CLASS(codec))->suspend(codec);
}

/**
 * Update a codec state.
 *
 * Checks the current state of the codec and the associated bus.
 *
 * @param codec Pointer to a Codec object.
 * @return Bus status: @b true if the bus is busy this codec's operation,
 * @b false if the bus is idle and available for use by other devices.
 */
static inline bool codecUpdate(void *codec)
{
  return ((const struct CodecClass *)CLASS(codec))->update(codec);
}

/**
 * Set a callback function to be invoked in case of errors.
 *
 * Registers a function that will be called when an error occurs during
 * codec operations. The callback can be disabled by passing NULL.
 *
 * @param codec Pointer to a Codec object.
 * @param callback Callback function to handle errors.
 * Pass NULL to disable error notifications.
 * @param argument User‑defined argument passed to the callback function.
 */
static inline void codecSetErrorCallback(void *codec, void (*callback)(void *),
    void *argument)
{
  ((const struct CodecClass *)CLASS(codec))->setErrorCallback(codec,
      callback, argument);
}

/**
 * Set a callback which is called when all operations are done successfully.
 *
 * Registers a function to be called when the codec completes its operations
 * without errors and enters an idle state.
 *
 * @param codec Pointer to a Codec object.
 * @param callback Callback function for idle notifications.
 * Pass NULL to disable idle notifications.
 * @param argument User‑defined argument passed to the callback function.
 */
static inline void codecSetIdleCallback(void *codec, void (*callback)(void *),
    void *argument)
{
  ((const struct CodecClass *)CLASS(codec))->setIdleCallback(codec,
      callback, argument);
}

/**
 * Set a callback for update requests.
 *
 * Registers a function to be called when the codec needs a state update.
 * It is not possible to use both an update callback and
 * a work queue simultaneously.
 *
 * @param codec Pointer to a Codec object.
 * @param callback Callback function for update requests.
 * Pass NULL to disable the update callback.
 * @param argument User‑defined argument passed to the callback function.
 */
static inline void codecSetUpdateCallback(void *codec,
    void (*callback)(void *), void *argument)
{
  ((const struct CodecClass *)CLASS(codec))->setUpdateCallback(codec,
      callback, argument);
}

/**
 * Set a work queue for update tasks.
 *
 * Assigns a work queue where update tasks are added when the codec needs
 * a state update. Using a work queue excludes the use of an update callback.
 *
 * @param codec Pointer to a Codec object.
 * @param wq Pointer to the work queue structure.
 * Pass NULL to disable the work queue.
 */
static inline void codecSetUpdateWorkQueue(void *codec, struct WorkQueue *wq)
{
  ((const struct CodecClass *)CLASS(codec))->setUpdateWorkQueue(codec, wq);
}

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_AUDIO_CODEC_H_ */
