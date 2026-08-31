/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "UMDAdaptor.h"

#include <log/log.h>

#include "umd-logging.h"
#include "umd.h"

#define LOG_TAG "UmdAdaptor_aidl"

using namespace std;

namespace aidl {
namespace vendor {
namespace qti {
namespace hardware {
namespace umd_aidl {


UMDAdaptor::UMDAdaptor(const std::string& role): role_(role) {
  if (role_ != "audio" && role_ != "camera") {
    UMD_LOG_INFO("Invalid role: %s", role_.c_str());
  }
  UMD_LOG_INFO("UMDAdaptor starting, role=%s", role_.c_str());
}

shared_ptr<IUMDAdaptorCallback> callbackV1_0 = nullptr;

ndk::ScopedAStatus UMDAdaptor::initUVC(int32_t* _aidl_return) {
  UMD_LOG_INFO("Initialization of UVC\n");
  if (role_ != "camera")
    return ndk::ScopedAStatus::ok();
  UMD_LOG_INFO("Initialization of UVC, camera role\n");
  init_uvc();
  UMD_LOG_INFO("starting UVC\n");
  int32_t res = start_uvc();
  *_aidl_return = res;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus UMDAdaptor::deInitUVC() {
  if (role_ != "camera")
    return ndk::ScopedAStatus::ok();
  UMD_LOG_INFO("UVC terminate\n");
  stop_uvc();
  deinit_uvc();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus UMDAdaptor::initUAC(const shared_ptr<IUMDAdaptorCallback>&
                 callback, int32_t* _aidl_return) {
  if (role_ != "audio")
    return ndk::ScopedAStatus::ok();
  UMD_LOG_INFO("Initialization of UAC \n");
  callbackV1_0 = callback;
  EventCallback uevent_cb = [&](AudioState newstate) { callbackV1_0->
      onAudioUevent((AudioStatus)(int32_t)newstate); };
  int32_t cbret = 0;
  AudioCallback cb = [&](std::vector<uint8_t> data) { callbackV1_0->
      onAudioBufferReceive(data, &cbret); };
  UMD_LOG_INFO("init_uac called\n");
  int res = init_uac(cb);
  int32_t retVal = 0;
  if (res)
    retVal = res;
  res = uevent_monitor(uevent_cb);
  if (res)
    retVal = res;
  *_aidl_return = retVal;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus UMDAdaptor::deInitUAC() {
  if (role_ != "audio")
    return ndk::ScopedAStatus::ok();
  UMD_LOG_INFO("deInitUAC called\n");
  UMD_LOG_INFO("UAC terminate\n");
  deinit_uac();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus UMDAdaptor::submitAudioBuffer(const vector<uint8_t>& data,
                int32_t* _aidl_return) {
  if (role_ != "audio")
    return ndk::ScopedAStatus::ok();
  UMD_LOG_INFO("Initialization of submitAudioBuffer \n");
  *_aidl_return = submit_buffer((uint8_t*)data.data());
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus UMDAdaptor::setAudioBufferSize(int64_t size) {
  if (role_ != "audio")
    return ndk::ScopedAStatus::ok();
  UMD_LOG_INFO("Initialization of setAudioBufferSize\n");
  set_buffersize(size);
  return ndk::ScopedAStatus::ok();
}

}  // namespace umd_aidl
}  // namespace hardware
}  // namespace qti
}  // namespace vendor
}  // namespace aidl
