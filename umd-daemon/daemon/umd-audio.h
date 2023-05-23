/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <string.h>
#include <tinyalsa/asoundlib.h>

#include <atomic>
#include <memory>
#include <thread>

#include "audio-pcm.h"
#include "audio-recorder-interface.h"
#include "audio-stream.h"
#include "umd-util.h"

using namespace android;

class UmdAudio {
 public:
  UmdAudio(std::string audiodev,
           AudioDirection audiodirection);
  UmdAudio(std::string audiodev,
           AudioDirection audiodirection,
           AudioCallback cb);
  ~UmdAudio();

  int32_t Init();
  int32_t Start();
  void Stop();
  void SetBufSize(size_t bufSize);
  int32_t SubmitBuffer(uint8_t *data);
  bool GetUmdStatus();

 private:
  void AudioThreadHandler();
  int32_t GetPcmCardDetails(std::string mAudioDev, unsigned int &pcm_card,
                            unsigned int &pcm_dev);

  std::string mAudioDev;
  struct pcm *mPcmHandle;
  AudioDirection mAudioDirection;
  std::unique_ptr<std::thread> mThread;
  std::atomic<bool> mRecording;
  size_t mBufSize;
  std::unique_ptr<AudioStream> mAudioStream;
  std::unique_ptr<PcmNode> mPcmNode;
  std::mutex mMutex;
  AudioCallback mCallback;
  std::atomic<bool> mStatus;
};