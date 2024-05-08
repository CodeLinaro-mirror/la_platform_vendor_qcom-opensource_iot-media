/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "umd-fake-camera.h"
#include "umd-logging.h"

#define LOG_TAG "UmdFakeCamera"

#define READ_INTERVAL 33

const uint32_t VIDEO_BUFFER_TIMEOUT = 1000; // [ms]

using std::chrono::milliseconds;

FakeCamera::FakeCamera(std::string uvcdev)
  : mGadget(nullptr),
    mVsetup({}),
    mUmdVideoCallbacks({
        FakeCamera::setupVideoStream,
        FakeCamera::enableVideoStream,
        FakeCamera::disableVideoStream,
        FakeCamera::handleVideoControl}),
    mUvcDev(uvcdev),
    mActive(false),
    mVideoBufferQueue(VIDEO_BUFFER_TIMEOUT) {}

FakeCamera::~FakeCamera() {}

void FakeCamera::cameraThreadHandler() {
  bool running = true;
  while (running) {
    FakeCameraMessage event;
    mMsg.pop(event);
    switch (event) {
      case FakeCameraMessage::CAMERA_START:
        UMD_LOG_DEBUG("CAMERA_START\n");
        if (!Start()) {
          UMD_LOG_ERROR("Camera start failed.\n");
        }
        break;
      case FakeCameraMessage::CAMERA_STOP:
        UMD_LOG_DEBUG("CAMERA_STOP\n");
        Stop();
        break;
      case FakeCameraMessage::CAMERA_TERMINATE:
        UMD_LOG_DEBUG("CAMERA_TERMINATE\n");
        Stop();
        running = false;
        break;
      default:
        UMD_LOG_ERROR("Unknown event type: %d", event);
    }
  }
}

int32_t FakeCamera::Init() {
  if (mUvcDev.empty()) {
    UMD_LOG_ERROR("video device does not exist!\n");
    return -ENODEV;
  }

  mGadget = umd_gadget_new(mUvcDev.c_str(), &mUmdVideoCallbacks, this);
  if (nullptr == mGadget) {
    UMD_LOG_ERROR("Failed to create UMD gadget!\n");
    return -ENODEV;
  }

  mFakeCameraThread = std::unique_ptr<std::thread>(
      new std::thread(&FakeCamera::cameraThreadHandler, this));

  if (nullptr == mFakeCameraThread) {
    UMD_LOG_ERROR("Camera thread creation failed!\n");
    return -ENOMEM;
  }

  return 0;
}

void FakeCamera::Deinit() {
  mMsg.push(FakeCameraMessage::CAMERA_TERMINATE);
  mActive = false;

  if (mFakeCameraThread) {
    mFakeCameraThread->join();
    mFakeCameraThread = nullptr;
  }

  if (mTriggerThread) {
    mTriggerThread->join();
    mTriggerThread = nullptr;
  }

  if (mReadThread) {
    mReadThread->join();
    mReadThread = nullptr;
  }

  mVideoBufferQueue.abort();

  if (mVideoBufferThread) {
    mVideoBufferThread->join();
    mVideoBufferThread = nullptr;
  }

  if (mGadget != nullptr)
    umd_gadget_free(mGadget);
}

void FakeCamera::triggerThreadHandler() {
  UMD_LOG_DEBUG("triggerThreadHandler: %s: entering trigger thread", __func__);

  // signal the read thread at a constant interval
  while (mActive) {
    usleep(READ_INTERVAL* 1000);
    mTriggerCV.notify_one();
  }

  UMD_LOG_DEBUG("triggerThreadHandler: %s: exiting trigger thread", __func__);
}

void FakeCamera::readThreadHandler() {
  UMD_LOG_DEBUG("readThreadHandler: %s: entering trigger thread", __func__);

  while (mActive) {
    {
      std::unique_lock<std::mutex> lock(mTriggerLock);
      milliseconds timeout = std::chrono::milliseconds(500);
      auto ret = mTriggerCV.wait_for(lock, timeout);
      if (ret == std::cv_status::timeout) {
        break;
      }
     }
     read_frame(ifs);
  }

  UMD_LOG_DEBUG("readThreadHandler: %s: exiting trigger thread", __func__);
}

bool FakeCamera::setupVideoStream(UmdVideoSetup * stmsetup, void * userdata) {
  FakeCamera *ctx = static_cast<FakeCamera*>(userdata);

  UMD_LOG_INFO ("Stream setup: %ux%u@%.2f - %c%c%c%c\n", stmsetup->width,
      stmsetup->height, stmsetup->fps, UMD_FMT_NAME (stmsetup->format));

  ctx->mVsetup = *stmsetup;
  return true;
}

bool FakeCamera::enableVideoStream(void * userdata) {
  UMD_LOG_DEBUG ("Stream ON\n");
  FakeCamera *ctx = static_cast<FakeCamera*>(userdata);

  ctx->mActive = true;
  ctx->mMsg.push(FakeCameraMessage::CAMERA_START);
  return true;
}

bool FakeCamera::disableVideoStream(void * userdata) {
  UMD_LOG_DEBUG ("Stream Off\n");
  FakeCamera *ctx = static_cast<FakeCamera*>(userdata);
  ctx->mActive = false;
  ctx->mMsg.push(FakeCameraMessage::CAMERA_STOP);
  return true;
}

bool FakeCamera::handleVideoControl(uint32_t id, uint32_t request,
  void* payload, void* userdata) {

  FakeCamera* ctx = static_cast<FakeCamera*>(userdata);

  UMD_LOG_INFO("Control: 0x%X, Request: 0x%X\n", id, request);
  return true;
}

int32_t FakeCamera::Start() {
  switch (mVsetup.format) {
    case UMD_VIDEO_FMT_YUYV:
      mFilename = "/data/yuy2_" + std::to_string(mVsetup.height) + "_" +
          std::to_string(mVsetup.width) + ".yuy2";
      break;
    case UMD_VIDEO_FMT_MJPEG:
      mFilename = "/data/mjpeg_" + std::to_string(mVsetup.height) + "_" +
          std::to_string(mVsetup.width) + ".mjpeg";
      break;
#ifdef ENABLE_H264
    case UMD_VIDEO_FMT_H264:
      mFilename = "/data/h264_" + std::to_string(mVsetup.height) + "_" +
          std::to_string(mVsetup.width) + ".h264";
      break;
#endif
    default:
      UMD_LOG_ERROR("Unsupported video format: %d!\n", mVsetup.format);
      return -1;
      break;
  }

  ifs.open(mFilename, std::ios::ate | std::ios::binary);

  mVideoBufferThread = std::unique_ptr<std::thread>(
      new std::thread(&FakeCamera::videoBufferLoop, this));

  if (nullptr == mVideoBufferThread) {
    UMD_LOG_ERROR ("Video buffer thread creation failed!\n");
    return -ENOMEM;
  }

  mTriggerThread = std::unique_ptr<std::thread>(
      new std::thread(&FakeCamera::triggerThreadHandler, this));

  if (nullptr == mTriggerThread) {
    UMD_LOG_ERROR("Trigger thread creation failed!\n");
    return -ENOMEM;
  }

  mReadThread = std::unique_ptr<std::thread>(
      new std::thread(&FakeCamera::readThreadHandler, this));

  if (nullptr == mReadThread) {
    UMD_LOG_ERROR("Read thread creation failed!\n");
    return -ENOMEM;
  }

  return true;
}

void FakeCamera::Stop() {

  if (mTriggerThread) {
    mTriggerThread->join();
    mTriggerThread = nullptr;
  }

  if (mReadThread) {
    mReadThread->join();
    mReadThread = nullptr;
  }

  if (mVideoBufferThread == nullptr) {
    UMD_LOG_ERROR ("Video loop thread not started!\n");
    return;
  }

  if (ifs)
    ifs.close();

  mVideoBufferQueue.abort();

  mVideoBufferThread->join();
  mVideoBufferThread = nullptr;

}

void FakeCamera::read_frame(std::ifstream &ifs) {
  uint32_t size = 0;
  static uint64_t timestamp = 0;
  ifs.read((char *)&size, 4);
  // if EOF
  if (size == 0) {
    ifs.close();
    ifs.open(mFilename, std::ios::ate | std::ios::binary);
    if (ifs) {
      ifs.seekg(0, ifs.end);
      ifs.seekg(0, ifs.beg);
    }
    return;
  }

  uint8_t *buffer = new uint8_t[size];
  ifs.read((char *)buffer, size);
  uint32_t bufidx = umd_gadget_submit_buffer(mGadget, UMD_VIDEO_STREAM_ID, buffer,
      size, size, timestamp);
  timestamp += READ_INTERVAL * 1000;
  mVideoBufferQueue.push(std::make_pair(buffer, bufidx));
}

void FakeCamera::videoBufferLoop() {
  while (mActive || mVideoBufferQueue.size()) {
    std::pair<uint8_t *, int32_t> buffer_pair;
    if (!mVideoBufferQueue.pop(buffer_pair)) {
      uint8_t *buffer = buffer_pair.first;
      int32_t bufidx = buffer_pair.second;
      umd_gadget_wait_buffer(mGadget, UMD_VIDEO_STREAM_ID, bufidx);
      if (buffer == nullptr) {
        UMD_LOG_ERROR("Invalid buffer handle\n");
        continue;
      }
      delete[] buffer;
    }
  }
  UMD_LOG_INFO("videoBufferLoop terminate!\n");
}