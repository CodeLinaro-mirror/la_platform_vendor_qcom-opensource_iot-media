/*
 * Copyright (c) 2018, 2019, 2021 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef USE_AIDL_ALLOCATOR
#include "allocator_aidl_interface.h"
#else
#include "allocator_hidl_interface.h"
#endif

#include "camera_memory_interface.h"
#include "utils/camera_log.h"

const int IMemAllocUsage::kHwCameraZsl      = (1 << 0);
const int IMemAllocUsage::kPrivateAllocUbwc = (1 << 1);
const int IMemAllocUsage::kPrivateIommUHeap = (1 << 2);
const int IMemAllocUsage::kPrivateMmHeap    = (1 << 3);
const int IMemAllocUsage::kPrivateUncached  = (1 << 4);
const int IMemAllocUsage::kProtected        = (1 << 5);
const int IMemAllocUsage::kSwReadOften      = (1 << 6);
const int IMemAllocUsage::kSwWriteOften     = (1 << 7);
const int IMemAllocUsage::kVideoEncoder     = (1 << 8);
const int IMemAllocUsage::kHwFb             = (1 << 9);
const int IMemAllocUsage::kHwTexture        = (1 << 10);
const int IMemAllocUsage::kHwRender         = (1 << 11);
const int IMemAllocUsage::kHwComposer       = (1 << 12);
const int IMemAllocUsage::kHwCameraRead     = (1 << 13);
const int IMemAllocUsage::kHwCameraWrite    = (1 << 14);

IAllocDevice *AllocDeviceFactory::CreateAllocDevice() {
#ifdef USE_AIDL_ALLOCATOR
  AidlAllocDevice *device = new AidlAllocDevice;
  if (!device->IsValid()) {
    CAMERA_ERROR("%s: Failed to create AIDL alloc device\n", __func__);
    delete device;
    return nullptr;
  }
  return device;
#else
  return new HidlAllocDevice;
#endif
}

void AllocDeviceFactory::DestroyAllocDevice(IAllocDevice* alloc_device_interface) {
  delete alloc_device_interface;
}

const IMemAllocUsage &AllocUsageFactory::GetAllocUsage() {
#ifdef USE_AIDL_ALLOCATOR
  static const AidlAllocUsage x = AidlAllocUsage();
#else
  static const HidlAllocUsage x = HidlAllocUsage();
#endif
  return x;
}

buffer_handle_t &GetAllocBufferHandle(const IBufferHandle &handle) {
#ifdef USE_AIDL_ALLOCATOR
  AidlAllocBuffer *b = static_cast<AidlAllocBuffer *>(handle);
#else
  HidlAllocBuffer *b = static_cast<HidlAllocBuffer *>(handle);
#endif
  assert(b != nullptr);
  return b->GetNativeHandle();
}
