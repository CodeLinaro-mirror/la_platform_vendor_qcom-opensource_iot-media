/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>
#include "UMDAdaptor.h"

int main(int, char**) {
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    namespace umd_aidl  = ::aidl::vendor::qti::hardware::umd_aidl;
    std::shared_ptr<umd_aidl::IUMDAdaptor> camera_sv =
        ndk::SharedRefBase::make<umd_aidl::UMDAdaptor>(/*role=*/"camera");

    const std::string name = std::string() + umd_aidl::IUMDAdaptor::descriptor + "/camera";
    if (AServiceManager_addService(camera_sv->asBinder().get(), name.c_str()) != STATUS_OK) {
        ALOGE("UMD CAMERA AIDL: register failed: %s", name.c_str());
        return 1;
    }
    ALOGI("UMD CAMERA AIDL: registered %s", name.c_str());
    ABinderProcess_joinThreadPool();
    return 0;
}
