/*
 * audio/codec.h
 * Copyright (C) 2023 xent
 * Project is distributed under the terms of the MIT License
 */

#ifndef DPM_AUDIO_CODEC_H_
#define DPM_AUDIO_CODEC_H_
/*----------------------------------------------------------------------------*/
#include <xcore/entity.h>
#include <stdbool.h>
#include <stdint.h>
/*----------------------------------------------------------------------------*/
struct WorkQueue;

enum CodecChannel
{
  CHANNEL_BOTH,
  CHANNEL_LEFT,
  CHANNEL_RIGHT
};

/* Class descriptor */
struct CodecClass
{
  CLASS_HEADER

  /* Getters */
  uint8_t (*getInputGain)(const void *, enum CodecChannel);
  uint8_t (*getOutputGain)(const void *, enum CodecChannel);
  bool (*isAGCEnabled)(const void *);
  bool (*isReady)(const void *);

  /* Setters */
  void (*setAGCEnabled)(void *, bool);
  void (*setInputGain)(void *, enum CodecChannel, uint8_t);
  void (*setInputPath)(void *, int);
  void (*setOutputGain)(void *, enum CodecChannel, uint8_t);
  void (*setOutputPath)(void *, int);
  void (*setSampleRate)(void *, uint32_t);

  void (*setErrorCallback)(void *, void (*)(void *), void *);
  void (*setIdleCallback)(void *, void (*)(void *), void *);
  void (*setUpdateCallback)(void *, void (*)(void *), void *);
  void (*setUpdateWorkQueue)(void *, struct WorkQueue *);

  void (*reset)(void *, uint32_t, int, int);
  bool (*update)(void *);
};

struct Codec
{
  struct Entity base;
};
/*----------------------------------------------------------------------------*/
BEGIN_DECLS

/**
 * Get current input gain of a selected channel.
 * @param codec Pointer to a Codec object.
 * @param channel Input channel.
 * @return Input gain in the range from 0 to 255, where 0 means
 * that the channel is muted.
 */
static inline uint8_t codecGetInputGain(const void *codec,
    enum CodecChannel channel)
{
  return ((const struct CodecClass *)CLASS(codec))->getInputGain(codec,
      channel);
}

/**
 * Get output gain of a selected channel.
 * @param codec Pointer to a Codec object.
 * @param channel Output channel.
 * @return Output gain in the range from 0 to 255, where 0 means
 * that the channel is muted.
 */
static inline uint8_t codecGetOutputGain(const void *codec,
    enum CodecChannel channel)
{
  return ((const struct CodecClass *)CLASS(codec))->getOutputGain(codec,
      channel);
}

/**
 * Get automatic gain control status for an input channel.
 * @param codec Pointer to a Codec object.
 * @return AGC status.
 */
static inline bool codecIsAGCEnabled(const void *codec)
{
  return ((const struct CodecClass *)CLASS(codec))->isAGCEnabled(codec);
}

/**
 * Get codec status.
 * @param codec Pointer to a Codec object.
 * @return Codec status, @b true when the codec is ready.
 */
static inline bool codecIsReady(const void *codec)
{
  return ((const struct CodecClass *)CLASS(codec))->isReady(codec);
}

/**
 * Enable or disable automatic gain control for an input channel.
 * @param codec Pointer to a Codec object.
 * @param state AGC state.
 */
static inline void codecSetAGCEnabled(void *codec, bool state)
{
  ((const struct CodecClass *)CLASS(codec))->setAGCEnabled(codec, state);
}

/**
 * Set input gain for a selected channel.
 * @param codec Pointer to a Codec object.
 * @param channel Input channel.
 * @param gain Input gain in the range from 0 to 255.
 */
static inline void codecSetInputGain(void *codec, enum CodecChannel channel,
    uint8_t gain)
{
  ((const struct CodecClass *)CLASS(codec))->setInputGain(codec, channel,
      gain);
}

/**
 * Set input path.
 * @param codec Pointer to a Codec object.
 * @param path Input path.
 */
static inline void codecSetInputPath(void *codec, int path)
{
  ((const struct CodecClass *)CLASS(codec))->setInputPath(codec, path);
}

/**
 * Set output gain for a selected channel.
 * @param codec Pointer to a Codec object.
 * @param channel Output channel.
 * @param gain Output gain in the range from 0 to 255.
 */
static inline void codecSetOutputGain(void *codec, enum CodecChannel channel,
    uint8_t gain)
{
  ((const struct CodecClass *)CLASS(codec))->setOutputGain(codec, channel,
      gain);
}

/**
 * Set output path.
 * @param codec Pointer to a Codec object.
 * @param path Output path.
 */
static inline void codecSetOutputPath(void *codec, int path)
{
  ((const struct CodecClass *)CLASS(codec))->setOutputPath(codec, path);
}

/**
 * Set sample rate for all input and output channels.
 * @param codec Pointer to a Codec object.
 * @param rate Sample rate.
 */
static inline void codecSetSampleRate(void *codec, uint32_t rate)
{
  ((const struct CodecClass *)CLASS(codec))->setSampleRate(codec, rate);
}

/**
 * Reset a codec.
 * @param codec Pointer to a Codec object.
 * @param rate Sample rate.
 * @param inputPath Input path.
 * @param outputPath Output path.
 */
static inline void codecReset(void *codec, uint32_t rate, int inputPath,
    int outputPath)
{
  ((const struct CodecClass *)CLASS(codec))->reset(codec, rate, inputPath,
      outputPath);
}

/**
 * Update a codec state.
 * @param codec Pointer to a Codec object.
 * @return Bus status, @b true when the bus is busy and @b false when the bus
 * is idle and may be used by another device.
 */
static inline bool codecUpdate(void *codec)
{
  return ((const struct CodecClass *)CLASS(codec))->update(codec);
}

/**
 * Set a callback which is called in case of errors.
 * @param codec Pointer to a Codec object.
 * @param callback Callback function.
 * @param argument Callback argument.
 */
static inline void codecSetErrorCallback(void *codec, void (*callback)(void *),
    void *argument)
{
  ((const struct CodecClass *)CLASS(codec))->setErrorCallback(codec,
      callback, argument);
}

/**
 * Set a callback which is called when all operations are done successfully.
 * @param codec Pointer to a Codec object.
 * @param callback Callback function.
 * @param argument Callback argument.
 */
static inline void codecSetIdleCallback(void *codec, void (*callback)(void *),
    void *argument)
{
  ((const struct CodecClass *)CLASS(codec))->setIdleCallback(codec,
      callback, argument);
}

/**
 * Set a callback for update requests.
 * Update request callback is called when the codec needs a state update.
 * It is not possible to use both an update callback and a work queue.
 * @param codec Pointer to a Codec object.
 * @param callback Callback function.
 * @param argument Callback argument.
 */
static inline void codecSetUpdateCallback(void *codec,
    void (*callback)(void *), void *argument)
{
  ((const struct CodecClass *)CLASS(codec))->setUpdateCallback(codec,
      callback, argument);
}

/**
 * Set a work queue for update tasks.
 * An update task is added to the work queue is used when the codec needs
 * a state update. It is not possible to use both an update callback
 * and a work queue.
 * @param codec Pointer to a Codec object.
 * @param callback Callback function.
 * @param argument Callback argument.
 */
static inline void codecSetUpdateWorkQueue(void *codec, struct WorkQueue *wq)
{
  ((const struct CodecClass *)CLASS(codec))->setUpdateWorkQueue(codec, wq);
}

END_DECLS
/*----------------------------------------------------------------------------*/
#endif /* DPM_AUDIO_CODEC_H_ */
