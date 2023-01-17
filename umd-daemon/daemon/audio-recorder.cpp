/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 */

#include "audio-recorder.h"

#include "umd-logging.h"

#define LOG_TAG "AudioRecorder"

const uint32_t AUDIO_BUFFERS_COUNT = 4;

AudioRecorder::AudioRecorder(std::string audiodevcapture,
                             std::string audiodevplayback,
                             AudioDirection audiodirection)
  : mAudioDevCapture(audiodevcapture),
    mAudioDevPlayback(audiodevplayback),
    mPcmCaptureHandle(nullptr),
    mPcmPlaybackHandle(nullptr),
    mMixer(nullptr),
    mAudioDirection(audiodirection),
    mThread(nullptr),
    mRecording(false),
    mBufSize(0),
    mAudioStream(nullptr),
    mErrorCode(0) {}

AudioRecorder::~AudioRecorder() {
  Stop();
}

int32_t AudioRecorder::Start() {
  const std::lock_guard<std::mutex> lock(mMutex);
  unsigned int pcm_card_in, pcm_dev_in;
  unsigned int pcm_card_out, pcm_dev_out;
  unsigned int mixer_card;
  mErrorCode = 0;

  int resCapture = GetPcmCardDetails(mAudioDevCapture, pcm_card_in, pcm_dev_in);
  if (resCapture)
    return resCapture;
  int resPlayback = GetPcmCardDetails(mAudioDevPlayback, pcm_card_out, pcm_dev_out);
  if (resPlayback)
    return resPlayback;

  mixer_card = (mAudioDirection == AUDIO_DEVICE_TO_HOST) ? pcm_card_in :
                pcm_card_out;
  mMixer = mixer_open(mixer_card);
  if (mMixer == nullptr) {
    UMD_LOG_ERROR ("Mixer device open failed!\n");
    return -ENODEV;
  }

  if (SetMixerConfiguration(mMixer, mAudioDirection) != 0) {
    UMD_LOG_ERROR ("Audio mixer configuration failed!\n");
    return -EINVAL;
  }

  mPcmNodeCapture = std::unique_ptr<PcmNode>(
      new PcmNode(pcm_card_in, pcm_dev_in, AUDIO_PCM_CAPTURE, mAudioDirection));

  mPcmCaptureHandle = mPcmNodeCapture->Open();
  if (mPcmCaptureHandle == nullptr) {
    UMD_LOG_ERROR ("Pcm open of Capture node failed!\n");
    return -ENODEV;
  }

  mPcmNodePlayback = std::unique_ptr<PcmNode>(
      new PcmNode(pcm_card_out, pcm_dev_out, AUDIO_PCM_PLAYBACK, mAudioDirection));
  mPcmPlaybackHandle = mPcmNodePlayback->Open();
  if (mPcmPlaybackHandle == nullptr) {
    UMD_LOG_ERROR ("Pcm open of Playback node failed!\n");
    return -ENODEV;
  }

  mBufSize = mPcmNodeCapture->GetBufferSize();
  if (mBufSize == 0) {
    UMD_LOG_ERROR ("Invalid audio buffer size!\n");
    return -EINVAL;
  }

  mAudioStream = std::unique_ptr<AudioStream>(
      new AudioStream(mBufSize, AUDIO_BUFFERS_COUNT, mPcmNodePlayback));

  if (mAudioStream == nullptr) {
    UMD_LOG_ERROR ("Audio stream creation failed!\n");
    return -ENOMEM;
  }

  int32_t res = mAudioStream->Init();
  if (res) {
    UMD_LOG_ERROR ("Audio stream init failed!\n");
    return res;
  }

  if (mThread == nullptr) {
    mRecording = true;
    mThread = std::unique_ptr<std::thread>(
       new std::thread(&AudioRecorder::AudioThreadHandler, this));
  }

  if (mThread == nullptr) {
    UMD_LOG_ERROR ("Audio thread creation failed!\n");
    return -ENOMEM;
  }

  return 0;
}

int32_t AudioRecorder::GetPcmCardDetails(std::string mAudioDev,
                                         unsigned int &pcm_card,
                                         unsigned int &pcm_dev) {
  if (mAudioDev[0] != 'h' ||
      mAudioDev[1] != 'w' ||
      mAudioDev[2] != ':' ||
      mAudioDev.length() < 4) {
    UMD_LOG_ERROR ("Invalid device name %s\n", mAudioDev.c_str());
    return -EINVAL;
  }

  if (sscanf(&mAudioDev[3], "%u,%u", &pcm_card, &pcm_dev) != 2) {
    UMD_LOG_ERROR ("Invalid device name %s\n", mAudioDev.c_str());
    return -EINVAL;
  }
  return 0;
}

int32_t AudioRecorder::Stop() {
  const std::lock_guard<std::mutex> lock(mMutex);

  mRecording = false;

  if (mThread != nullptr) {
    mThread->join();
    mThread = nullptr;
  }

  if (mMixer != nullptr) {
    if (MixerRelease(mMixer, mAudioDirection) != 0) {
      UMD_LOG_ERROR ("Audio mixer release failed!\n");
    }
    mixer_close(mMixer);
    mMixer = nullptr;
  }

  if (mPcmCaptureHandle != nullptr) {
    mPcmNodeCapture->Close();
    mPcmCaptureHandle = nullptr;
  }

  if (mPcmPlaybackHandle != nullptr) {
    mPcmNodePlayback->Close();
    mPcmPlaybackHandle = nullptr;
  }

  mAudioStream = nullptr;

  return mErrorCode;
}

void AudioRecorder::AudioThreadHandler() {
  while (mRecording) {
    AudioBuffer *buffer;
    int32_t res = mAudioStream->GetBuffer(&buffer);
    if (res) {
      UMD_LOG_ERROR ("Audio stream get buffer failed.\n");
      mErrorCode = res;
      break;
    }

    if (buffer == nullptr) {
      UMD_LOG_ERROR ("Invalid audio stream buffer.\n");
      mErrorCode = -ENOMEM;
      break;
    }

    res = mPcmNodeCapture->Read(buffer);
    if (!res) {
      struct timespec ts;
      unsigned int avail = 0;
      if (mPcmNodeCapture->GetTimeStamp(&avail, &ts)) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
      }
      buffer->timestamp = ts.tv_sec * 1000000000LL + ts.tv_nsec;
      buffer->size = mBufSize;
      mAudioStream->SubmitBuffer(buffer);
    } else {
      UMD_LOG_ERROR ("pcm_read fail: %s\n", pcm_get_error(mPcmCaptureHandle));
      mAudioStream->ReturnBuffer(buffer);
      mErrorCode = res;
    }
  }
}

int32_t AudioRecorder::SetMixerConfiguration(struct mixer *mixer,
                                             AudioDirection audiodirection) {
  struct mixer_ctl *ctl = nullptr;
  int32_t ret = 0;

  if (audiodirection == AUDIO_DEVICE_TO_HOST) {
    ctl = mixer_get_ctl_by_name(mixer, "TX_CDC_DMA_TX_3 Channels");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_enum_by_string(ctl, "One");
    if (ret) {
      return ret;
    }

    ctl = mixer_get_ctl_by_name(mixer, "TX_AIF1_CAP Mixer DEC2");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_value(ctl, 0, 1);
    if (ret) {
      return ret;
    }

    ctl = mixer_get_ctl_by_name(mixer, "TX DMIC MUX2");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_enum_by_string(ctl, "DMIC0");
    if (ret) {
      return ret;
    }

    ctl = mixer_get_ctl_by_name(mixer, "TX_DEC2 Volume");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_value(ctl, 0, 112);
    if (ret) {
      return ret;
    }

    ctl = mixer_get_ctl_by_name(mixer, "MultiMedia1 Mixer TX_CDC_DMA_TX_3");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_value(ctl, 0, 1);
    if (ret) {
      return ret;
    }
  } else {
    ctl = mixer_get_ctl_by_name(mixer, "QUAT_MI2S_RX Audio Mixer MultiMedia1");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_value(ctl, 0, 1);
    if (ret) {
      return ret;
    }
  }

  return 0;
}

int32_t AudioRecorder::MixerRelease(struct mixer *mixer,
                                    AudioDirection audiodirection) {
  struct mixer_ctl *ctl = nullptr;
  int32_t ret = 0;
  if (audiodirection == AUDIO_DEVICE_TO_HOST) {
    ctl = mixer_get_ctl_by_name(mixer, "TX_AIF1_CAP Mixer DEC2");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_value(ctl, 0, 0);
    if (ret) {
      return ret;
    }

    ctl = mixer_get_ctl_by_name(mixer, "TX DMIC MUX2");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_enum_by_string(ctl, "ZERO");
    if (ret) {
      return ret;
    }

    ctl = mixer_get_ctl_by_name(mixer, "MultiMedia1 Mixer TX_CDC_DMA_TX_3");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_value(ctl, 0, 0);
    if (ret) {
      return ret;
    }
  } else {
    ctl = mixer_get_ctl_by_name(mixer, "QUAT_MI2S_RX Audio Mixer MultiMedia1");
    if (ctl == nullptr) {
      return -ENODEV;
    }

    ret = mixer_ctl_set_value(ctl, 0, 0);
    if (ret) {
      return ret;
    }
  }

  return 0;
}