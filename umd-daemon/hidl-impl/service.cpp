/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "vendor.qti.hardware.umd@1.0-service"

#include <vendor/qti/hardware/umd/1.0/IUMDAdaptor.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>
#include <utils/StrongPointer.h>
#include "UMDAdaptor.h"

// libhwbinder
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

// Generated HIDL files
using vendor::qti::hardware::umd::V1_0::IUMDAdaptor;
using vendor::qti::hardware::umd::V1_0::implementation::UMDAdaptor;

int main(int /* argc */, char** /* argv */) {
    configureRpcThreadpool(1, true);

    android::sp<IUMDAdaptor> umd = new UMDAdaptor();
    if (umd->registerAsService() != ::android::OK) {
      ALOGE("Failed to register UMD HAL instance");
        return -1;
    }

    joinRpcThreadpool();
    return 1;  // joinRpcThreadpool shouldn't exit
}
