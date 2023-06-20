/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <string.h>
#include <tinyalsa/asoundlib.h>

#include "audio-recorder-interface.h"
#include "umd-logging.h"

class PcmNode {
public:
  PcmNode(unsigned int card,
          unsigned int device,
          AudioPcmMode pcmmode,
          AudioDirection audiodirection);
  ~PcmNode();

  struct pcm *Open();
  void Close();
  int Read(AudioBuffer *buffer);
  int Write(AudioBuffer *buffer);
  int IsReady();
  size_t GetBufferSize();
  int GetTimeStamp(unsigned int *avail, struct timespec *ts);
  void PrintPcmNodeInfo();

private:
  unsigned int mCard;
  unsigned int mDevice;
  AudioPcmMode mPcmMode;
  struct pcm_config mConfig;

  struct pcm *mPcm;
  size_t mBufSize;
};