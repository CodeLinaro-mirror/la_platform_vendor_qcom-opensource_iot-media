/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "umd.h"

#include "umd-audio.h"
#include "umd-camera.h"
#include "umd-logging.h"

#define LOG_TAG "UmdLibAdaptor"

android::sp<UmdCamera> umdcam;
std::unique_ptr<UmdAudio> audioPlayback;
std::unique_ptr<UmdAudio> audioCapture;
std::unique_ptr<UmdUtil> umdUtil;
std::unique_ptr<std::thread> audioThread;

bool audioActive = false;
const int32_t CAM_ID = 0;
const std::string UVC_DEV = "/dev/video2";
const std::string HOST_DEV = "hw:1,0";

void init_uvc() {
  std::string uvc_dev;
  int32_t cameraID;
  cameraID = Property::Get("persist.vendor.umd.uvc.camid", CAM_ID);
  uvc_dev = Property::Get("persist.vendor.umd.uvc.dev", UVC_DEV);
  umdcam = new UmdCamera(uvc_dev, cameraID);
}

int32_t start_uvc() {
  int res = umdcam->StartUVC();
  if (res) {
    UMD_LOG_ERROR("Start UVC failed\n");
    return -1;
  }
  return 0;
}

void stop_uvc() {
  umdcam->StopUVC();
}

void deinit_uvc() {
  if (umdcam)
    umdcam = nullptr;
}

void deinit_uac() {
  audioActive = false;
  if (audioThread != nullptr) {
    audioThread->join();
    audioThread = nullptr;
  }
  if (audioPlayback != nullptr) {
    audioPlayback.reset();
    audioPlayback = nullptr;
  }
  if (audioCapture != nullptr) {
    audioCapture.reset();
    audioCapture = nullptr;
  }
}

void set_buffersize(size_t bufSize) {
  audioPlayback->SetBufSize(bufSize);
}

int32_t submit_buffer(uint8_t *data) {
  if (audioPlayback->GetUmdStatus()) {
    int32_t res = audioPlayback->SubmitBuffer(data);
    if (res) {
      UMD_LOG_ERROR("Submit buffer to umd-audio failed!\n");
      return res;
    }
  }
  return 0;
}

void change_audio_status(AudioState newstate, EventCallback uevent_cb) {
  switch (newstate) {
    case AUDIO_STATE_PAUSED:
      audioPlayback->Stop();
      audioCapture->Stop();
      break;
    case AUDIO_STATE_CAPTURE:
      audioPlayback->Stop();
      audioCapture->Start();
      break;
    case AUDIO_STATE_PLAYBACK:
      audioPlayback->Start();
      audioCapture->Stop();
      break;
    case AUDIO_STATE_PLAYBACK_CAPTURE:
      audioPlayback->Start();
      audioCapture->Start();
      break;
  }
  uevent_cb(newstate);
}

int32_t init_uac(AudioCallback cb) {
  std::string host_dev;
  int32_t res = 0;
  host_dev = Property::Get("persist.vendor.umd.uac.host.dev", HOST_DEV);
  if (audioPlayback == nullptr) {
    audioPlayback = std::unique_ptr<UmdAudio>(new UmdAudio(
        host_dev, AUDIO_DEVICE_TO_HOST));
    if (audioPlayback == nullptr) {
      UMD_LOG_ERROR("AudioPlayback creation failed!\n");
      return -ENOMEM;
    }
    res = audioPlayback->Init();
    if (res) {
      UMD_LOG_ERROR("AudioPlayback init failed!\n");
      return -1;
    }
  }

  if (audioCapture == nullptr) {
    audioCapture = std::unique_ptr<UmdAudio>(new UmdAudio(
        host_dev, AUDIO_HOST_TO_DEVICE, cb));
    if (audioCapture == nullptr) {
      UMD_LOG_ERROR("AudioCapture creation failed!\n");
      return -ENOMEM;
    }
    res = audioCapture->Init();
    if (res) {
      UMD_LOG_ERROR("AudioCapture init failed!\n");
      return -1;
    }
  }
  return 0;
}

void audioThreadHandler(EventCallback uevent_cb, UmdEventCallback umd_uevent_cb) {
  while (audioActive) {
    umdUtil->monitor_audio_status(uevent_cb, umd_uevent_cb);
  }
}

int32_t uevent_monitor(EventCallback uevent_cb) {
  UmdEventCallback umd_uevent_cb = change_audio_status;
  umdUtil = std::unique_ptr<UmdUtil>(new UmdUtil());
  if (umdUtil == nullptr) {
    UMD_LOG_ERROR("UmdUtil object creation failed!\n");
    return -ENOMEM;
  }

  if (nullptr == audioThread) {
    audioActive = true;
    UMD_LOG_INFO("Creating Audio thread\n");
    audioThread = std::unique_ptr<std::thread>(
        new std::thread(audioThreadHandler, uevent_cb, umd_uevent_cb));
    if (nullptr == audioThread) {
      UMD_LOG_ERROR("Audio thread creation failed!\n");
      audioActive = false;
      return -ENOMEM;
    }
  }
  return 0;
}
