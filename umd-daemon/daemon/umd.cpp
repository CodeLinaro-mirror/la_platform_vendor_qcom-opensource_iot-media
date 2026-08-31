/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include<regex>

#include "umd.h"
#include "umd-audio.h"
#include "umd-camera.h"
#include "umd-logging.h"
#include "umd-fake-camera.h"

#define LOG_TAG "UmdLibAdaptor"

std::unique_ptr<UmdAudio> audioPlayback;
std::unique_ptr<UmdAudio> audioCapture;
std::unique_ptr<UmdUtil> umdUtil;
std::unique_ptr<std::thread> audioThread;

bool audioActive = false;
const std::string HOST_DEV = "hw:1,0";
std::vector<std::unique_ptr<FakeCamera>> fakecamInstances;
std::vector<std::unique_ptr<UmdCamera>> umdcamInstances;

void init_uvc() {
  std::string uvc_dev;
  int32_t gadget_cnt;
  uint32_t num_of_cameras;
  uint32_t filebased_uvc;
  num_of_cameras = Property::Get("persist.vendor.umd.num.cam", 1);
  filebased_uvc = Property::Get("persist.vendor.umd.file.based", 0);
  uint32_t video_dev_base = Property::Get("persist.vendor.umd.video.dev.base", 2);

  for (uint32_t i = 0; i < num_of_cameras; i++) {
    uvc_dev = "/dev/video" + std::to_string(i + video_dev_base);
    umdcamInstances.push_back(std::unique_ptr<UmdCamera>(new UmdCamera(uvc_dev, i)));
  }

  if (!filebased_uvc)
    return;
  gadget_cnt = get_gadget_cnt();
  if (gadget_cnt < 0)
    return;
  UMD_LOG_INFO("MultiUVC filebased usecase\n");
  for (uint32_t i = 0; i < gadget_cnt - num_of_cameras; i++) {
    uvc_dev = "/dev/video" + std::to_string(num_of_cameras + i + video_dev_base);
    fakecamInstances.push_back(std::unique_ptr<FakeCamera>(new FakeCamera(uvc_dev)));
  }
}

int32_t start_uvc() {
  int32_t res;
  auto filebased_uvc = Property::Get("persist.vendor.umd.file.based", 0);
  for (const auto &ptr : umdcamInstances) {
    res = ptr->StartUVC();
    if (res) {
      UMD_LOG_ERROR("Start UVC failed\n");
      ptr->StopUVC();
      deinit_uvc();
      return -1;
    }
  }

  if (!filebased_uvc)
    return 0;
  int32_t gadget_cnt = get_gadget_cnt();
  if (gadget_cnt < 0) {
    for (const auto &ptr : umdcamInstances)
      ptr->StopUVC();
    deinit_uvc();
    return -1;
  }
  for (const auto &ptr : fakecamInstances) {
    res = ptr->Init();
    if (res) {
      UMD_LOG_ERROR("Start MultiUVC failed\n");
      for (const auto &ptr : umdcamInstances)
        ptr->StopUVC();
      deinit_uvc();
      return -1;
    }
  }

  return 0;
}

void stop_uvc() {
  for (const auto &ptr : umdcamInstances)
    ptr->StopUVC();

  auto filebased_uvc = Property::Get("persist.vendor.umd.file.based", 0);
  if (!filebased_uvc)
    return;

  int32_t gadget_cnt = get_gadget_cnt();
  if (gadget_cnt < 0)
    return;

  for (const auto &ptr : fakecamInstances) {
    ptr->Deinit();
  }
}

void deinit_uvc() {
  umdcamInstances.clear();

  auto filebased_uvc = Property::Get("persist.vendor.umd.file.based", 0);
  if (!filebased_uvc)
    return;

  int32_t gadget_cnt = get_gadget_cnt();
  if (gadget_cnt < 0)
    return;

  fakecamInstances.clear();
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
  host_dev = Property::Get("persist.vendor.umd.uac.host.dev", HOST_DEV);
  if (audioPlayback == nullptr) {
    audioPlayback = std::unique_ptr<UmdAudio>(new UmdAudio(
        host_dev, AUDIO_DEVICE_TO_HOST));
    if (audioPlayback == nullptr) {
      UMD_LOG_ERROR("AudioPlayback creation failed!\n");
      return -ENOMEM;
    }
  }

  if (audioCapture == nullptr) {
    audioCapture = std::unique_ptr<UmdAudio>(new UmdAudio(
        host_dev, AUDIO_HOST_TO_DEVICE, cb));
    if (audioCapture == nullptr) {
      UMD_LOG_ERROR("AudioCapture creation failed!\n");
      return -ENOMEM;
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

int32_t get_gadget_cnt() {
  std::string usb_mode;
  std::smatch match;
  int32_t count = 0;
  std::ifstream file("/config/usb_gadget/g1/configs/b.1/strings/0x409/configuration");
  if (!file.is_open()) {
    UMD_LOG_ERROR("Failed to open usb composition file");
    return -1;
  }
  std::getline(file, usb_mode);
  file.close();
  std::regex pattern("(\\d+)(?=xuvc)");

  if (std::regex_search(usb_mode, match, pattern))
    count = std::stoi(match.str());

  return count;
}
