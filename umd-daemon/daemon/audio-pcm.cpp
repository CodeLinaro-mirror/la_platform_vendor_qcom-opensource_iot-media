/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "audio-pcm.h"
#include "umd-util.h"

#define LOG_TAG "AudioPCM"

const uint32_t UAC_PLAYBACK_SAMPLE_RATE = 48000;
const uint32_t UAC_CAPTURE_SAMPLE_RATE = 64000;
const uint32_t UAC_AUDIO_PERIOD_SIZE = 1024;
const uint32_t UAC_AUDIO_PERIOD_COUNT = 4;
const uint32_t UAC_AUDIO_NUM_CHANNELS = 2;

PcmNode::PcmNode(unsigned int card,
                 unsigned int device,
                 AudioPcmMode pcmmode,
                 AudioDirection audiodirection)
  : mCard(card),
    mDevice(device),
    mPcmMode(pcmmode) {
  mConfig.period_size = UAC_AUDIO_PERIOD_SIZE;
  mConfig.period_count = UAC_AUDIO_PERIOD_COUNT;

  mConfig.silence_threshold = 0;
  mConfig.format = PCM_FORMAT_S16_LE;
  if (audiodirection == AUDIO_HOST_TO_DEVICE) {
    mConfig.rate = Property::Get("persist.vendor.umd.cp.srate",
        UAC_CAPTURE_SAMPLE_RATE);
    mConfig.channels = Property::Get("persist.vendor.umd.cp.chmask",
        UAC_AUDIO_NUM_CHANNELS);
  } else {
    mConfig.rate = Property::Get("persist.vendor.umd.pb.srate",
        UAC_PLAYBACK_SAMPLE_RATE);
    mConfig.channels = Property::Get("persist.vendor.umd.pb.chmask",
        UAC_AUDIO_NUM_CHANNELS);
  }

  if (mPcmMode == AUDIO_PCM_CAPTURE) {
    mConfig.stop_threshold = 0;
    mConfig.start_threshold = 0;
  } else {
    mConfig.stop_threshold = mConfig.period_size * mConfig.period_count;
    mConfig.start_threshold = mConfig.period_size * mConfig.period_count;
  }
}

PcmNode::~PcmNode() {}

struct pcm *PcmNode::Open() {
  if (mPcmMode == AUDIO_PCM_CAPTURE)
    mPcm = pcm_open(mCard, mDevice, PCM_IN | PCM_MONOTONIC, &mConfig);
  else
    mPcm = pcm_open(mCard, mDevice, PCM_OUT, &mConfig);

  return mPcm;
}

void PcmNode::Close() {
  pcm_close(mPcm);
}

size_t PcmNode::GetBufferSize() {
  mBufSize = pcm_frames_to_bytes(mPcm, pcm_get_buffer_size(mPcm));
  return mBufSize;
}

int PcmNode::Read(AudioBuffer *buffer) {
  int res = pcm_read(mPcm, buffer->data, mBufSize);
  return res;
}

int PcmNode::Write(AudioBuffer *buffer) {
  int res = pcm_write(mPcm, buffer->data, buffer->size);
  if (res < 0)
    UMD_LOG_ERROR("pcm_write fail: %s\n", pcm_get_error(mPcm));
  return res;
}

int PcmNode::IsReady() {
  return pcm_is_ready(mPcm);
}

int PcmNode::GetTimeStamp(unsigned int *avail, struct timespec *ts) {
  return pcm_get_htimestamp(mPcm, avail, ts);
}

void PcmNode::PrintPcmNodeInfo() {
  UMD_LOG_INFO("Pcm card = %d Pcm device = %d Samplerate = %d Channels = %d\n",
      mCard, mDevice, mConfig.rate, mConfig.channels);
}