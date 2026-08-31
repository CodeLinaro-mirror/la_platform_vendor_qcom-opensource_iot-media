/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include<binder/Status.h>
#include "aidl/vendor/qti/hardware/umd_aidl/BnUMDAdaptor.h"

#include "umd-util.h"
using namespace std;
namespace aidl {
namespace vendor {
namespace qti {
namespace hardware {
namespace umd_aidl {

typedef std::function<void(std::vector<uint8_t> data)> AudioCallback;
typedef std::function<void(AudioState status)> EventCallback;

class UMDAdaptor : public BnUMDAdaptor {
public:
  explicit UMDAdaptor(const std::string& role);
  ndk::ScopedAStatus initUVC(int32_t* _aidl_return) override;
  ndk::ScopedAStatus deInitUVC() override;
  ndk::ScopedAStatus initUAC(const shared_ptr<aidl::vendor::qti::hardware::umd_aidl::
                          IUMDAdaptorCallback>& callback, int32_t* _aidl_return) override;
  ndk::ScopedAStatus deInitUAC() override;
  ndk::ScopedAStatus submitAudioBuffer(const vector<uint8_t>& data, int32_t* _aidl_return) override;
  ndk::ScopedAStatus setAudioBufferSize(int64_t size) override;

private:
  const std::string role_;
};

}  // namespace umd_aidl
}  // namespace hardware
}  // namespace qti
}  // namespace vendor
}  // namespace aidl
