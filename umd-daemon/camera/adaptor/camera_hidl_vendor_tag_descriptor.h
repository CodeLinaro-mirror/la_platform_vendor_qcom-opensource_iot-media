/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <VendorTagDescriptor.h>
#include <android/hardware/camera/common/1.0/types.h>
#include <aidl/android/hardware/camera/common/VendorTagSection.h>
#include<vector>

using ::android::hardware::camera::common::V1_0::helper::VendorTagDescriptor;
using ::aidl::android::hardware::camera::common::VendorTagSection;

class CustomVendorTagDescriptor : public VendorTagDescriptor {
 public:
   static android::status_t createDescriptorFromHidl(
           const std::vector<VendorTagSection> &vts,
           android::sp<VendorTagDescriptor>& descriptor);
   static android::status_t createDescriptorFromAidl(
           const std::vector<VendorTagSection>& vts,
           android::sp<VendorTagDescriptor>& descriptor);
};
