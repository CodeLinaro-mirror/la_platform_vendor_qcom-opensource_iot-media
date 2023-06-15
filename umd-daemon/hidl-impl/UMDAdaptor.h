/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include <vendor/qti/hardware/umd/1.0/IUMDAdaptor.h>

#include "umd-util.h"
namespace vendor {
namespace qti {
namespace hardware {
namespace umd {
namespace V1_0 {
namespace implementation {

using ::android::sp;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

typedef std::function<void(std::vector<uint8_t> data)> AudioCallback;
typedef std::function<void(AudioState status)> EventCallback;

struct UMDAdaptor : public IUMDAdaptor {
  Return<int32_t> initUVC() override;
  Return<void> deInitUVC() override;
  Return<int32_t> initUAC(const sp<::vendor::qti::hardware::umd::V1_0::
                          IUMDAdaptorCallback>& callback) override;
  Return<void> deInitUAC() override;
  Return<int32_t> submitAudioBuffer(const hidl_vec<uint8_t>& data) override;
  Return<void> setAudioBufferSize(uint64_t size) override;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace umd
}  // namespace hardware
}  // namespace qti
}  // namespace vendor
