/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <camera_device_client.h>
#include <umd-gadget.h>

#include <string>
#include <thread>

#include "c2-module.h"
#include "camera_common_utils.h"
#include "message_queue.h"

using namespace ::android;
using namespace ::camera::adaptor;
using namespace ::camera;

class UmdVideoData {
public:
  virtual ~UmdVideoData() {};
  virtual bool init() = 0;
  virtual void processData(StreamBuffer buffer) = 0;
  virtual bool deinit() = 0;
  IAllocDevice* mAllocDeviceInterface;
  std::unique_ptr<std::thread> mVideoBufferThread;
  bool mActive;
  UmdGadget *mGadget;
  C2Module *mC2Module;
  sp<Camera3DeviceClient> mDeviceClient;
  uint32_t streamId;
};

class UmdBufferMap {
private:
  std::map<int32_t, StreamBuffer> bufferMap;
  std::mutex mapMutex;

public:
  void insert(uint64_t key, StreamBuffer buffer) {
    std::lock_guard<std::mutex> guard(mapMutex);
    bufferMap[key] = buffer;
  }

  void erase(uint64_t key) {
    std::lock_guard<std::mutex> guard(mapMutex);
    auto it = bufferMap.find(key);
    if (it != bufferMap.end()) {
      bufferMap.erase(it);
    }
  }

  StreamBuffer& find(int key) {
    StreamBuffer buffer;
    std::lock_guard<std::mutex> guard(mapMutex);
    auto it = bufferMap.find(key);
    if (it != bufferMap.end()) {
      return it->second;
    }
    return buffer;
  }
};

// Factory class to create instances of UmdVideoData based on format
class UmdVideoDataFactory {
public:
  static  UmdVideoData * createUmdVideoInstance(UmdVideoSetup mVsetup,
                                                UmdGadget *gadget,
                                                C2Module *C2Module,
                                                sp<Camera3DeviceClient> deviceClient);
};
