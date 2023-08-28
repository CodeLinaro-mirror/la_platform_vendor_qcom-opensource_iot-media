/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "umd-audio.h"

#include "umd-logging.h"

#define LOG_TAG "UmdAudio"

const uint32_t AUDIO_BUFFERS_COUNT = 8;

UmdAudio::UmdAudio(std::string audiodev,
                   AudioDirection audiodirection)
    : mAudioDev(audiodev),
      mPcmHandle(nullptr),
      mAudioDirection(audiodirection),
      mThread(nullptr),
      mBufSize(0),
      mAudioStream(nullptr),
      mStatus(false) {}

UmdAudio::UmdAudio(std::string audiodev,
                   AudioDirection audiodirection,
                   AudioCallback cb)
    : mAudioDev(audiodev),
      mPcmHandle(nullptr),
      mAudioDirection(audiodirection),
      mThread(nullptr),
      mRecording(false),
      mBufSize(0),
      mAudioStream(nullptr),
      mCallback(cb),
      mStatus(false) {}

UmdAudio::~UmdAudio() {}

int32_t UmdAudio::Start() {
  if (mStatus)
    return 0;

  unsigned int pcm_card, pcm_dev;
  int32_t result = GetPcmCardDetails(mAudioDev, pcm_card, pcm_dev);
  if (result) {
    UMD_LOG_ERROR("Failed to retreive pcm card details\n");
    return -1;
  }

  if (mAudioDirection == AUDIO_DEVICE_TO_HOST) {
    mPcmNode = std::unique_ptr<PcmNode>(
        new PcmNode(pcm_card, pcm_dev, AUDIO_PCM_PLAYBACK, mAudioDirection));
  } else {
    mPcmNode = std::unique_ptr<PcmNode>(
        new PcmNode(pcm_card, pcm_dev, AUDIO_PCM_CAPTURE, mAudioDirection));
  }

  if (mPcmNode == nullptr) {
    UMD_LOG_ERROR("Failed to create PcmNode\n");
    return -1;
  }
  mPcmNode->PrintPcmNodeInfo();
  mPcmHandle = mPcmNode->Open();
  if (mPcmHandle == nullptr) {
    UMD_LOG_ERROR("Pcm open failed!\n");
    return -1;
  } else {
    UMD_LOG_INFO("Pcm open success\n");
  }

  if (mAudioDirection == AUDIO_HOST_TO_DEVICE) {
    UMD_LOG_INFO("Start UAC - AUDIO_HOST_TO_DEVICE");
    mBufSize = mPcmNode->GetBufferSize();
    if (mBufSize == 0) {
      UMD_LOG_ERROR("Invalid audio buffer size!\n");
      return -EINVAL;
    }
    mAudioStream = std::unique_ptr<AudioStream>(
        new AudioStream(mBufSize, AUDIO_BUFFERS_COUNT, mPcmNode,
                        mAudioDirection, mCallback));
  } else {
    UMD_LOG_INFO("Start UAC - AUDIO_DEVICE_TO_HOST");
    mAudioStream = std::unique_ptr<AudioStream>(
        new AudioStream(mBufSize, AUDIO_BUFFERS_COUNT, mPcmNode,
                        mAudioDirection));
  }

  if (mAudioStream == nullptr) {
    UMD_LOG_ERROR("Audio stream creation failed!\n");
    return -ENOMEM;
  }

  int32_t res = mAudioStream->Init();
  if (res) {
    UMD_LOG_ERROR("Audio stream init failed!\n");
    return res;
  }

  if (mAudioDirection == AUDIO_HOST_TO_DEVICE) {
    if (mThread == nullptr) {
      mRecording = true;
      mThread = std::unique_ptr<std::thread>(
          new std::thread(&UmdAudio::AudioThreadHandler, this));
    }

    if (mThread == nullptr) {
      UMD_LOG_ERROR("Audio thread creation failed!\n");
      return -ENOMEM;
    }
  }

  mStatus = true;
  return 0;
}

void UmdAudio::Stop() {
  if (!mStatus)
    return;

  mStatus = false;

  if (mAudioDirection == AUDIO_HOST_TO_DEVICE) {
    UMD_LOG_INFO("Stop UAC - AUDIO_HOST_TO_DEVICE");
    mRecording = false;
    if (mThread != nullptr) {
      mThread->join();
      mThread = nullptr;
    }
  } else {
    UMD_LOG_INFO("Stop UAC - AUDIO_DEVICE_TO_HOST");
  }
  {
    const std::lock_guard<std::mutex> lock(mMutex);
    mAudioStream.reset();
    mAudioStream = nullptr;
  }

  mPcmNode->Close();
  if(mPcmNode!=nullptr)
    mPcmNode = nullptr;

}

void UmdAudio::SetBufSize(size_t bufSize) {
  mBufSize = bufSize;
}

int32_t UmdAudio::GetPcmCardDetails(std::string mAudioDev,
                                    unsigned int &pcm_card,
                                    unsigned int &pcm_dev) {
  if (mAudioDev[0] != 'h' ||
      mAudioDev[1] != 'w' ||
      mAudioDev[2] != ':' ||
      mAudioDev.length() < 4) {
    UMD_LOG_ERROR("Invalid device name %s\n", mAudioDev.c_str());
    return -EINVAL;
  }

  if (sscanf(&mAudioDev[3], "%u,%u", &pcm_card, &pcm_dev) != 2) {
    UMD_LOG_ERROR("Invalid device name %s\n", mAudioDev.c_str());
    return -EINVAL;
  }
  return 0;
}

void UmdAudio::AudioThreadHandler() {
  while (mRecording) {
    AudioBuffer *buffer;
    const std::lock_guard<std::mutex> lock(mMutex);
    int32_t res = mAudioStream->GetBuffer(&buffer);
    if (res) {
      UMD_LOG_ERROR("Audio stream get buffer failed.\n");
      break;
    }

    if (buffer == nullptr) {
      UMD_LOG_ERROR("Invalid audio stream buffer.\n");
      break;
    }
    res = mPcmNode->Read(buffer);
    if (!res) {
      struct timespec ts;
      unsigned int avail = 0;
      if (mPcmNode->GetTimeStamp(&avail, &ts)) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
      }
      buffer->timestamp = ts.tv_sec * 1000000000LL + ts.tv_nsec;
      buffer->size = mBufSize;
      mAudioStream->SubmitBuffer(buffer);
    } else {
      UMD_LOG_ERROR("pcm_read fail: %s\n", pcm_get_error(mPcmHandle));
      mAudioStream->ReturnBuffer(buffer);
    }
  }
}

int32_t UmdAudio::SubmitBuffer(uint8_t *data) {
  AudioBuffer *buffer = nullptr;
  const std::lock_guard<std::mutex> lock(mMutex);
  int32_t res = mAudioStream->GetBuffer(&buffer);
  if (res) {
    UMD_LOG_ERROR("Audio stream get buffer failed.\n");
    return -1;
  }
  memcpy(buffer->data, data, mBufSize);
  buffer->size = mBufSize;
  mAudioStream->SubmitBuffer(buffer);
  return 0;
}

bool UmdAudio::GetUmdStatus() {
  return mStatus;
}
