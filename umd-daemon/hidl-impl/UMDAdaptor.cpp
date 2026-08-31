/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "UMDAdaptor.h"

#include <log/log.h>

#include "umd-logging.h"
#include "umd.h"

#define LOG_TAG "UmdAdaptor_hidl"

namespace vendor {
namespace qti {
namespace hardware {
namespace umd {
namespace V1_0 {
namespace implementation {
sp<IUMDAdaptorCallback> callbackV1_0 = nullptr;

Return<int32_t> UMDAdaptor::initUVC() {
  UMD_LOG_INFO("Initialization of UVC\n");
  init_uvc();
  int32_t res = start_uvc();
  return res;
}

Return<void> UMDAdaptor::deInitUVC() {
  UMD_LOG_INFO("UVC terminate\n");
  stop_uvc();
  deinit_uvc();
  return Void();
}

Return<int32_t> UMDAdaptor::initUAC(const sp<::vendor::qti::hardware::umd::V1_0
                                    ::IUMDAdaptorCallback>& callback) {
  UMD_LOG_INFO("Initialization of UAC\n");
  callbackV1_0 = callback;
  EventCallback uevent_cb = [&](AudioState newstate) { callbackV1_0->
      onAudioUevent((AudioStatus)(int)newstate); };
  AudioCallback cb = [&](std::vector<uint8_t> data) { callbackV1_0->
      onAudioBufferReceive(data); };
  int32_t res = init_uac(cb);
  int32_t retVal = 0;
  if (res)
    retVal = res;
  res = uevent_monitor(uevent_cb);
  if (res)
    retVal = res;
  return retVal;
}

Return<void> UMDAdaptor::deInitUAC() {
  UMD_LOG_INFO("UAC terminate\n");
  deinit_uac();
  return Void();
}

Return<int32_t> UMDAdaptor::submitAudioBuffer(const hidl_vec<uint8_t>& data) {
  int32_t res = submit_buffer((uint8_t*)data.data());
  return res;
}

Return<void> UMDAdaptor::setAudioBufferSize(uint64_t size) {
  set_buffersize(size);
  return Void();
}
}  // namespace implementation
}  // namespace V1_0
}  // namespace umd
}  // namespace hardware
}  // namespace qti
}  // namespace vendor
