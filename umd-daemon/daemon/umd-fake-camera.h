/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <unistd.h>

#include <umd-gadget.h>
#include "message_queue.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <fstream>

enum class FakeCameraMessage {
  CAMERA_START,
  CAMERA_STOP,
  CAMERA_SUBMIT_REQUEST,
  CAMERA_TERMINATE
};

class FakeCamera {
public:
  ~FakeCamera();
  FakeCamera(std::string uvcdev);
  int32_t Init();
  void Deinit();

private:
  static bool setupVideoStream(UmdVideoSetup * stmsetup, void * userdata);
  static bool enableVideoStream(void * userdata);
  static bool disableVideoStream(void * userdata);
  static bool handleVideoControl(uint32_t id, uint32_t request, void * payload,
                                      void * userdata);
  int32_t Start();
  void Stop();
  void triggerThreadHandler();
  void readThreadHandler();
  void cameraThreadHandler();
  void videoBufferLoop();
  void read_frame(std::ifstream& ifs);

  UmdGadget *mGadget;
  UmdVideoSetup mVsetup;
  UmdVideoCallbacks mUmdVideoCallbacks;
  std::string mUvcDev;

  std::unique_ptr<std::thread> mTriggerThread;
  std::unique_ptr<std::thread> mReadThread;
  std::mutex mTriggerLock;
  std::condition_variable mTriggerCV;
  std::string mFilename;

  std::atomic<bool> mActive;
  MessageQ<FakeCameraMessage> mMsg;
  std::unique_ptr<std::thread> mFakeCameraThread;
  std::unique_ptr<std::thread> mVideoBufferThread;
  MessageQ<std::pair<uint8_t*, int32_t>> mVideoBufferQueue;
  std::ifstream ifs;
};

