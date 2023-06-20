/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <vendor/qti/hardware/umd/1.0/IUMDAdaptorCallback.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

namespace vendor {
namespace qti {
namespace hardware {
namespace umd {
namespace V1_0 {
namespace implementation {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct UMDAdaptorCallback : public IUMDAdaptorCallback {
    Return<int32_t> onAudioBufferReceive(const hidl_vec<uint8_t>& data) override;
    Return<void> onAudioUevent(::vendor::qti::hardware::umd::V1_0::AudioStatus
                               status) override;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace umd
}  // namespace hardware
}  // namespace qti
}  // namespace vendor
