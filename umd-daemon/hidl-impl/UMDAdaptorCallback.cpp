/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "UMDAdaptorCallback.h"

namespace vendor {
namespace qti {
namespace hardware {
namespace umd {
namespace V1_0 {
namespace implementation {

Return<int32_t> UMDAdaptorCallback::onAudioBufferReceive(const hidl_vec<uint8_t>
                                                         & data) {
  return int32_t{};
}

Return<void> UMDAdaptorCallback::onAudioUevent(::vendor::qti::hardware::umd::
                                               V1_0::AudioStatus status) {
  return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace umd
}  // namespace hardware
}  // namespace qti
}  // namespace vendor
