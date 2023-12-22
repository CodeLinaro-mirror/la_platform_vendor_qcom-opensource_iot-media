/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
# Changes from Qualcomm Innovation Center are provided under the following license :
# Copyright(c) 2022-2023 Qualcomm Innovation Center, Inc.
#
# Redistributionand use in sourceand binary forms, with or without
# modification, are permitted(subject to the limitations in the
# disclaimer below) provided that the following conditions are met :
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditionsand the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditionsand the following
#      disclaimer in the documentationand /or other materials provided
#      with the distribution.
#
#    * Neither the name Qualcomm Innovation Center nor the names of its
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
# NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
# GRANTED BY THIS LICENSE.THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
# HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "umd-camera.h"
#include "umd-logging.h"

#include <VendorTagDescriptor.h>
#include <hardware/camera3.h>

#ifdef ENABLE_H264
#include <C2PlatformSupport.h>
#include <C2AllocatorGralloc.h>
#include <C2BlockInternal.h>
#include <gralloc_priv.h>
#endif

#define LOG_TAG "UmdCamera"

#ifndef JPEG_BLOB_OFFSET
#define JPEG_BLOB_OFFSET (0)
#endif

#define C2_COMPONENT_NAME "c2.qti.avc.encoder"
#define C2_RATE_CTRL_DISABLE 0x7F000000
#define C2_BITRATE 0xffffffff
#define UMD_VIDEO_CTRL_GET_PAN(X)    (((int32_t *)(&(X)))[0] / 3600)
#define UMD_VIDEO_CTRL_GET_TILT(X)   (((int32_t *)(&(X)))[1] / 3600)
#define UMD_VIDEO_CTRL_SET_PAN_AND_TILT(P, T) \
    ((((signed long)(P) * 3600) & 0xFFFFFFFF) | \
    ((((signed long)(T) * 3600) & 0xFFFFFFFF) << 32))

using ::android::hardware::camera::common::V1_0::helper::VendorTagDescriptor;

const uint32_t STREAM_BUFFER_COUNT = 10;
const uint32_t VIDEO_BUFFER_TIMEOUT = 1000; // [ms]
const uint32_t C2_OUT_FRAMERATE = 30;
const uint32_t C2_ROTATION_ANGLE = 180;
const uint32_t C2_PFRAME_VALUE = 29;
const uint32_t C2_BFRAME_VALUE = 0;
const uint32_t C2_REFRESH_PERIOD = 0;
const uint64_t FPS_TIME_INTERVAL = 3000000;
uint64_t UmdCamera::umd_current_pan_and_tilt = 0;
uint32_t umd_latency_log;

#ifdef ENABLE_H264
class UmdC2Notifier : public IC2Notifier {
 public:
  UmdC2Notifier(UmdFrameCallback frameCb) : mFrameCb(frameCb) {}
  void EventHandler(C2EventType event, void* payload) override {
    switch (event) {
      case C2EventType::kError:
        UMD_LOG_ERROR ("Received engine error\n");
        break;
      case C2EventType::kEOS:
        break;
      default:
        UMD_LOG_ERROR ("Unknown event '%u'!", static_cast<uint32_t>(event));
        break;
    }
  }

  void FrameAvailable(std::shared_ptr<C2Buffer>& c2buffer, uint64_t index,
                      uint64_t timestamp, C2FrameData::flags_t flags) override {

    const C2ConstLinearBlock block = c2buffer->data().linearBlocks().front();
    const C2ReadView view = block.map().get();
    mFrameCb((uint8_t*)view.data(), block.size(), timestamp);
  }

 private:
  UmdFrameCallback mFrameCb;
};

std::shared_ptr<C2Buffer> UmdCamera::ImportGraphicBuffer(StreamBuffer buffer) {
  uint64_t format = HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS;
  std::unique_ptr<private_handle_t> privateHandle;
  uint32_t height = buffer.info.plane_info[0].height;
  uint32_t width = buffer.info.plane_info[0].width;
  uint32_t stride = buffer.info.plane_info[0].stride;
  uint64_t usage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                   GRALLOC_USAGE_SW_READ_OFTEN;

  const private_handle_t* priv_handle =
      static_cast<const private_handle_t*>(GetAllocBufferHandle(buffer.handle));
  if (priv_handle == NULL) {
    UMD_LOG_ERROR ("Failed to create private_handle_t");
    return nullptr;
  }

  C2Handle* handle = android::WrapNativeCodec2GrallocHandle(
      (native_handle_t*)priv_handle, width, height, format, usage, stride);
  if (handle == nullptr) {
    UMD_LOG_ERROR ("Failed to create C2 handle");
    return nullptr;
  }

  std::shared_ptr<C2Allocator> allocator;
  std::shared_ptr<C2AllocatorStore> store =
      android::GetCodec2PlatformAllocatorStore();
  auto ret = store->fetchAllocator(
      android::C2PlatformAllocatorStore::DEFAULT_GRAPHIC, &allocator);
  if (ret != C2_OK || allocator == nullptr) {
    UMD_LOG_ERROR ("Failed to create C2 allocator");
    delete handle;
    return nullptr;
  }

  std::shared_ptr<C2GraphicAllocation> allocation;
  ret = allocator->priorGraphicAllocation(handle, &allocation);
  if (ret != C2_OK) {
    UMD_LOG_ERROR ("Prior Graphic allocation failed");
    delete handle;
    return nullptr;
  }

  std::shared_ptr<C2GraphicBlock> block =
      _C2BlockFactory::CreateGraphicBlock(allocation);
  if (!block) {
    UMD_LOG_ERROR ("Failed to create graphic block!");
    return nullptr;
  }

  auto c2buffer = C2Buffer::CreateGraphicBuffer(
      block->share(C2Rect(block->width(), block->height()), ::C2Fence()));
  if (!c2buffer) {
    UMD_LOG_ERROR ("Failed to create graphic C2 buffer!");
    return nullptr;
  }

  return c2buffer;
}
#endif

UmdCamera::UmdCamera(std::string uvcdev,int cameraId)
  : mGadget(nullptr),
    mVsetup({}),
    mUmdVideoCallbacks({
        UmdCamera::setupVideoStream,
        UmdCamera::enableVideoStream,
        UmdCamera::disableVideoStream,
        UmdCamera::handleVideoControl}),
    mUvcDev(uvcdev),
    mCameraId(cameraId),
    mStreamId(-1),
    mActive(false),
    mRequestId(-1),
    mDeviceClient(nullptr),
    mAllocDeviceInterface(nullptr),
    mClientCb({}),
    mLastFrameNumber(-1),
    mVideoBufferQueue(VIDEO_BUFFER_TIMEOUT),
    mCodecVideoBufferQueue(VIDEO_BUFFER_TIMEOUT),
    mCtrlValues({}),
    mRotation(StreamRotation::ROTATION_0) {
  GET_LATENCY_LOGS();
}

UmdCamera::~UmdCamera() {}

int32_t UmdCamera::StartUVC() {
  int32_t res = -1;
  if (!mUvcDev.empty()) {
    res = InitializeCamera();
    if (res != 0) {
      printf("InitializeCamera() failed. res: %d\n", res);
      return -ENODEV;
    }
  }

  mGadget = umd_gadget_new(mUvcDev.empty() ? nullptr : mUvcDev.c_str(),
      &mUmdVideoCallbacks, this);
  if (nullptr == mGadget) {
    UMD_LOG_ERROR ("Failed to create UMD gadget!\n");
    return -ENODEV;
  }

  mCameraThread = std::unique_ptr<std::thread>(
      new std::thread(&UmdCamera::cameraThreadHandler, this));

  if (nullptr == mCameraThread) {
    UMD_LOG_ERROR ("Camera thread creation failed!\n");
    return -ENOMEM;
  }

  return 0;
}

void UmdCamera::StopUVC() {
  mMsg.push(UmdCameraMessage::CAMERA_TERMINATE);
  mActive = false;
  if (mCameraThread) {
    mCameraThread->join();
  }

  if (mGadget != nullptr)
    umd_gadget_free (mGadget);

  if (nullptr != mAllocDeviceInterface) {
    AllocDeviceFactory::DestroyAllocDevice(mAllocDeviceInterface);
  }
}

int32_t UmdCamera::InitializeCamera() {

  mClientCb.errorCb = [&](
    CameraErrorCode errorCode,
    const CaptureResultExtras& extras) { ErrorCb(errorCode, extras); };

  mClientCb.idleCb = [&]() { IdleCb(); };

  mClientCb.peparedCb = [&](int id) { PreparedCb(id); };

  mClientCb.shutterCb = [&](const CaptureResultExtras& extras,
    int64_t ts) { ShutterCb(extras, ts); };

  mClientCb.resultCb = [&](const CaptureResult& result) { ResultCb(result); };

  mAllocDeviceInterface = AllocDeviceFactory::CreateAllocDevice();
  if (nullptr == mAllocDeviceInterface) {
    UMD_LOG_ERROR("Alloc device creation failed!\n");
    return -ENODEV;
  }

  mDeviceClient = new Camera3DeviceClient(mClientCb);
  if (nullptr == mDeviceClient.get()) {
    UMD_LOG_ERROR("Invalid camera device client!\n");
    return -ENOMEM;
  }

  auto ret = mDeviceClient->Initialize();
  if (ret) {
    UMD_LOG_ERROR("Camera client initialization failed!\n");
    return ret;
  }

  ret = mDeviceClient->OpenCamera(mCameraId);
  if (ret) {
    UMD_LOG_ERROR("Camera %d open failed!\n", mCameraId);
    return ret;
  }

  ret = mDeviceClient->GetCameraInfo(mCameraId, &mStaticInfo);
  if (ret) {
    UMD_LOG_ERROR("GetCameraInfo failed!\n");
    return ret;
  }

  // Get the sensor orientation
  auto entry = mStaticInfo.find(ANDROID_SENSOR_ORIENTATION);
  if (entry.count == 0)
    UMD_LOG_ERROR("ANDROID_SENSOR_ORIENTATION info is not available \n");
  else {
    UMD_LOG_INFO("Sensor orientation is %d \n",entry.data.i32[0]);
    switch (entry.data.i32[0]) {
    case 0:
      mRotation = StreamRotation::ROTATION_0;
      break;
    case 90:
      mRotation = StreamRotation::ROTATION_90;
      break;
    case 180:
      mRotation = StreamRotation::ROTATION_180;
      break;
    case 270:
      mRotation = StreamRotation::ROTATION_270;
      break;
    default:
      UMD_LOG_ERROR("Invalid Sensor Orientation \n");
      break;
    }
  }

  ret = mDeviceClient->CreateDefaultRequest(RequestTemplate::PREVIEW,
    &mRequest.metadata);
  if (ret) {
    UMD_LOG_ERROR("Camera CreateDefaultRequest failed!\n");
    return ret;
  }

  FillInitialControlValue();

  return 0;
}

bool UmdCamera::setupVideoStream(UmdVideoSetup * stmsetup, void * userdata) {
  UmdCamera *ctx = static_cast<UmdCamera*>(userdata);

  const std::lock_guard<std::mutex> lock(ctx->mCameraMutex);

  UMD_LOG_INFO ("Stream setup: %ux%u@%.2f - %c%c%c%c\n", stmsetup->width,
      stmsetup->height, stmsetup->fps, UMD_FMT_NAME (stmsetup->format));

  ctx->mVsetup = *stmsetup;
  return true;
}

bool UmdCamera::enableVideoStream(void * userdata) {
  UMD_LOG_DEBUG ("Stream ON\n");
  UmdCamera *ctx = static_cast<UmdCamera*>(userdata);

  ctx->mActive = true;
  ctx->mMsg.push(UmdCameraMessage::CAMERA_START);
  return true;
}

bool UmdCamera::disableVideoStream(void * userdata) {
  UMD_LOG_DEBUG ("Stream Off\n");
  UmdCamera *ctx = static_cast<UmdCamera*>(userdata);

  ctx->mMsg.push(UmdCameraMessage::CAMERA_STOP);
  ctx->mActive = false;
  return true;
}

uint32_t UmdCamera::GetVendorTagByName(const char * section, const char * name) {
  sp<VendorTagDescriptor> desc;
  uint32_t tag = 0;

  desc = VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (desc.get() == nullptr) {
    UMD_LOG_ERROR ("Failed to get Vendor Tag Descriptor!\n");
    return 0;
  }

  auto status = desc->lookupTag(android::String8(name),
      android::String8(section), &tag);
  if (status != 0) {
    UMD_LOG_ERROR ("Unable to find vendor tag for '%s', section '%s'!\n",
        name, section);
    return 0;
  }
  return tag;
}

bool UmdCamera::InitCameraParamsLocked() {
  CameraMetadata meta;
  if (!GetCameraMetadataLocked(meta)) {
    return false;
  }

  SetDefaultControlValues(meta);

  if (!SetCameraMetadataLocked(meta, false)) {
    UMD_LOG_ERROR("Set camera metadata failed!\n");
    return false;
  }

  return true;
}

void UmdCamera::SetExposureCompensation(CameraMetadata & meta, int16_t value)
{
  int32_t exposure = static_cast<int32_t>(value);
  meta.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &exposure, 1);
}

void UmdCamera::GetExposureCompensation(CameraMetadata & meta, int16_t * value)
{
  if (meta.exists(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION)) {
    *value = static_cast<int16_t>(
        meta.find(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION).data.i32[0]);
  }
}

void UmdCamera::SetContrast(CameraMetadata & meta, uint16_t value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.contrast", "level");
  if (tag != 0) {
    int32_t contrast = static_cast<int32_t>(value);
    meta.update(tag, &contrast, 1);
  }
}

void UmdCamera::GetContrast(CameraMetadata & meta, uint16_t * value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.contrast", "level");
  if (tag != 0 && meta.exists(tag)) {
    int32_t contrast = meta.find(tag).data.i32[0];
    *value = static_cast<uint16_t>(contrast);
  }
}

void UmdCamera::SetSaturation(CameraMetadata & meta, uint16_t value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.saturation", "use_saturation");
  if (tag != 0) {
    int32_t saturation = static_cast<int32_t>(value);
    meta.update(tag, &saturation, 1);
  }
}

void UmdCamera::GetSaturation(CameraMetadata & meta, uint16_t * value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.saturation", "use_saturation");

  if (tag != 0 && meta.exists(tag)) {
    *value = static_cast<int16_t>(
        meta.find(tag).data.i32[0]);
  }
}

void UmdCamera::SetSharpness(CameraMetadata & meta, uint16_t value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.sharpness", "strength");
  if (tag != 0) {
    int32_t sharpness = static_cast<int32_t>(value);
    meta.update(tag, &sharpness, 1);
  }
}

void UmdCamera::GetSharpness(CameraMetadata & meta, uint16_t * value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.sharpness", "strength");

  if (tag != 0 && meta.exists(tag)) {
    *value = static_cast<uint16_t>(
        meta.find(tag).data.i32[0]);
  }
}

void UmdCamera::SetADRC(CameraMetadata & meta, uint16_t value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.adrc", "disable");
  if (tag != 0) {
    uint8_t adrc = static_cast<uint8_t>(value);
    meta.update(tag, &adrc, 1);
  }
}

void UmdCamera::GetADRC(CameraMetadata & meta, uint16_t * value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.adrc", "disable");

  if (tag != 0 && meta.exists(tag)) {
    *value = meta.find(tag).data.u8[0];
  }
}

void UmdCamera::SetAntibanding(CameraMetadata & meta, uint8_t value) {
  uint8_t mode = 0;
  switch (value) {
    case UMD_VIDEO_ANTIBANDING_AUTO:
      mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
      break;
    case UMD_VIDEO_ANTIBANDING_DISABLED:
      mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF;
      break;
    case UMD_VIDEO_ANTIBANDING_60HZ:
      mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ;
      break;
    case UMD_VIDEO_ANTIBANDING_50HZ:
      mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ;
      break;
    default:
      UMD_LOG_ERROR ("Unsupported Antibanding mode: %d!\n", value);
      return;
  }
  meta.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &mode, 1);
}

bool UmdCamera::GetAntibanding(CameraMetadata & meta, uint8_t * value) {
  if (meta.exists(ANDROID_CONTROL_AE_ANTIBANDING_MODE)) {
    uint8_t mode = meta.find(ANDROID_CONTROL_AE_ANTIBANDING_MODE).data.u8[0];
    switch (mode) {
      case ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO:
        *value = UMD_VIDEO_ANTIBANDING_AUTO;
        break;
      case ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF:
        *value = UMD_VIDEO_ANTIBANDING_DISABLED;
        break;
      case ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ:
        *value = UMD_VIDEO_ANTIBANDING_60HZ;
        break;
      case ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ:
        *value = UMD_VIDEO_ANTIBANDING_50HZ;
        break;
      default:
        UMD_LOG_ERROR ("Unsupported Antibanding mode: %d!\n", mode);
        return false;
    }
  } else {
      return false;
  }
  return true;
}

void UmdCamera::SetISO(CameraMetadata & meta, uint16_t value) {
  int32_t priority = 0;

  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.iso_exp_priority", "select_priority");
  if (tag != 0) {
    meta.update(tag, &priority, 1);
  }

  tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_value");
  if (tag != 0) {
    int32_t isovalue = value;
    meta.update(tag, &isovalue, 1);
  }
}

void UmdCamera::GetISO(CameraMetadata & meta, uint16_t * value) {
    uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_value");
  if (tag != 0 && meta.exists(tag)) {
    *value = static_cast<uint16_t>(meta.find(tag).data.i32[0]);
  }
}

void UmdCamera::SetWbTemperature(CameraMetadata & meta, uint16_t value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.manualWB", "color_temperature");
   if (tag != 0) {
     int32_t color_temperature = static_cast<int32_t>(value);
     meta.update(tag, &color_temperature, 1);
   }
}

bool UmdCamera::GetWbTemperature(CameraMetadata & meta, uint16_t * value) {
  uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.manualWB", "color_temperature");
   if (tag != 0 && meta.exists(tag)) {
     *value = static_cast<uint16_t>(meta.find(tag).data.i32[0]);
     return true;
   }
   return false;
}

void UmdCamera::SetWbMode(CameraMetadata & meta, uint8_t value) {
  int32_t mode = PARTIAL_MWB_MODE_DISABLE;
  switch (value) {
    case UMD_VIDEO_WB_MODE_AUTO:
      mode = PARTIAL_MWB_MODE_DISABLE;
      break;
    case UMD_VIDEO_WB_MODE_MANUAL:
      mode = PARTIAL_MWB_MODE_CCT;
      break;
    default:
      UMD_LOG_ERROR ("\nUnsupported WB mode: %d!\n", value);
      return;
  }

  uint32_t tag = GetVendorTagByName (
      "org.codeaurora.qcamera3.manualWB", "partial_mwb_mode");
  if (tag != 0) {
    meta.update(tag, &mode, 1);
  }
}

bool UmdCamera::GetWbMode(CameraMetadata & meta, uint8_t * value) {
    uint32_t tag = GetVendorTagByName (
      "org.codeaurora.qcamera3.manualWB", "partial_mwb_mode");
  if (tag != 0 && meta.exists(tag)) {
    int32_t mode = meta.find(tag).data.i32[0];
    switch(mode) {
      case PARTIAL_MWB_MODE_DISABLE:
        *value = UMD_VIDEO_WB_MODE_AUTO;
        break;
      case PARTIAL_MWB_MODE_CCT:
        *value = UMD_VIDEO_WB_MODE_MANUAL;
        break;
      default:
        UMD_LOG_ERROR ("Unsupported WB mode: %d!\n", mode);
        return false;
    }
  } else {
      return false;
  }
  return true;
}

void UmdCamera::SetExposureTime(CameraMetadata & meta, uint32_t exposure) {
  int64_t exposure_time = exposure * 100000;
  meta.update(ANDROID_SENSOR_EXPOSURE_TIME, &exposure_time, 1);
}

void UmdCamera::GetExposureTime(CameraMetadata & meta, uint32_t * exposure)
{
  if (meta.exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
    int64_t exposure_time = meta.find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
    *exposure = static_cast<uint32_t>(exposure_time / 100000);
  }
}

void UmdCamera::SetExposureMode(CameraMetadata & meta, uint8_t mode) {
  uint8_t exposure_mode;
  switch (mode) {
    case UMD_VIDEO_EXPOSURE_MODE_AUTO:
      exposure_mode = ANDROID_CONTROL_AE_MODE_ON;
      break;
    case UMD_VIDEO_EXPOSURE_MODE_SHUTTER:
      exposure_mode = ANDROID_CONTROL_AE_MODE_OFF;
      break;
    default:
      UMD_LOG_ERROR ("Unsupported Exposure mode: %d!\n", mode);
      return;
  }
  meta.update(ANDROID_CONTROL_AE_MODE, &exposure_mode, 1);
}

bool UmdCamera::GetExposureMode(CameraMetadata & meta, uint8_t * mode)
{
  if (meta.exists(ANDROID_CONTROL_AE_MODE)) {
    uint8_t ae_mode = meta.find(ANDROID_CONTROL_AE_MODE).data.u8[0];
    if (ae_mode == ANDROID_CONTROL_AE_MODE_ON) {
      *mode = UMD_VIDEO_EXPOSURE_MODE_AUTO;
    } else if (ae_mode == ANDROID_CONTROL_AE_MODE_OFF) {
      *mode = UMD_VIDEO_EXPOSURE_MODE_SHUTTER;
    }
  } else {
      return false;
  }
  return true;
}

void UmdCamera::SetFocusMode(CameraMetadata & meta, uint8_t value) {
  uint8_t mode  = 0;
  switch (value) {
    case UMD_VIDEO_FOCUS_MODE_AUTO:
      mode = ANDROID_CONTROL_AF_MODE_AUTO;
      break;
    case UMD_VIDEO_FOCUS_MODE_MANUAL:
      mode = ANDROID_CONTROL_AF_MODE_OFF;
      break;
    default:
      UMD_LOG_ERROR ("Unsupported Focus mode: %d!\n", value);
      return;
  }
  meta.update(ANDROID_CONTROL_AF_MODE, &mode, 1);
}

bool UmdCamera::GetFocusMode(CameraMetadata & meta, uint8_t * value) {
  if (meta.exists(ANDROID_CONTROL_AF_MODE)) {
    uint8_t mode = meta.find(ANDROID_CONTROL_AF_MODE).data.u8[0];
    switch (mode) {
      case ANDROID_CONTROL_AF_MODE_AUTO:
        *value = UMD_VIDEO_FOCUS_MODE_AUTO;
        break;
      case ANDROID_CONTROL_AF_MODE_OFF:
        *value = UMD_VIDEO_FOCUS_MODE_MANUAL;
        break;
      default:
        UMD_LOG_ERROR ("Unsupported Focus mode: %d!\n", mode);
        return false;
    }
  }else {
      return false;
  }
  return true;
}

void UmdCamera::SetZoom(CameraMetadata& meta, uint16_t* in_magnification,
  uint64_t* pan_and_tilt, UVCControlValues& ctrl_vals) {

  int32_t sensor_x = 0, sensor_y = 0, sensor_w = 0, sensor_h = 0;
  int32_t zoom[4] = { 0 };
  static int32_t pan = 0, tilt = 0;
  uint16_t magnification = 0;

  if (pan_and_tilt) {
    pan = UMD_VIDEO_CTRL_GET_PAN(*pan_and_tilt);
    tilt = UMD_VIDEO_CTRL_GET_TILT(*pan_and_tilt);
    umd_current_pan_and_tilt = *pan_and_tilt;
  }
  else {
    pan = UMD_VIDEO_CTRL_GET_PAN(umd_current_pan_and_tilt);
    tilt = UMD_VIDEO_CTRL_GET_TILT(umd_current_pan_and_tilt);
  }

  if (in_magnification) {
    magnification = *in_magnification;
  } else {
    GetZoom(meta, &magnification);
  }

  if (mStaticInfo.exists(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE)) {
    sensor_x =
      mStaticInfo.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[0];
    sensor_y =
      mStaticInfo.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[1];
    sensor_w =
      mStaticInfo.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[2];
    sensor_h =
      mStaticInfo.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[3];
  }

  int32_t zoom_w = (sensor_w - sensor_x) / (magnification / 100.0);
  int32_t zoom_h = (sensor_h - sensor_y) / (magnification / 100.0);

  int64_t pan_min = UMD_VIDEO_CTRL_GET_PAN(ctrl_vals.pan_tilt_min);
  int64_t pan_max = UMD_VIDEO_CTRL_GET_PAN(ctrl_vals.pan_tilt_max);

  float pan_steps = (pan_max - pan_min) / 2.0;

  int32_t zoom_x = ((sensor_w - sensor_x) - zoom_w) / 2;
  zoom_x += (zoom_x * pan) / pan_steps;

  int64_t tilt_min = UMD_VIDEO_CTRL_GET_TILT(ctrl_vals.pan_tilt_min);
  int64_t tilt_max = UMD_VIDEO_CTRL_GET_TILT(ctrl_vals.pan_tilt_max);
  float tilt_steps = (tilt_max - tilt_min) / 2.0;

  int32_t zoom_y = ((sensor_h - sensor_y) - zoom_h) / 2;
  zoom_y += (zoom_y * tilt) / tilt_steps;

  zoom[0] = zoom_x;
  zoom[1] = zoom_y;
  zoom[2] = zoom_w;
  zoom[3] = zoom_h;

  meta.update(ANDROID_SCALER_CROP_REGION, zoom, 4);
}

void UmdCamera::GetZoom(CameraMetadata & meta, uint16_t * magnification) {
  int32_t zoom_x = 0, zoom_y = 0, zoom_w = 0, zoom_h = 0;
  int32_t sensor_x = 0, sensor_y = 0, sensor_w = 0, sensor_h = 0;

  if (!meta.exists(ANDROID_SCALER_CROP_REGION)) {
    UMD_LOG_ERROR("Scaller crop region metadata doesn't exist.\n");
    return;
  }

  if (!mStaticInfo.exists(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE)) {
    UMD_LOG_ERROR("Sensor info active array metadata tag doesn't exist.\n");
    return;
  }

  zoom_x = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[0];
  zoom_y = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[1];
  zoom_w = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[2];
  zoom_h = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[3];

  sensor_x = mStaticInfo.find (ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[0];
  sensor_y = mStaticInfo.find (ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[1];
  sensor_w = mStaticInfo.find (ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[2];
  sensor_h = mStaticInfo.find (ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[3];

  zoom_w = (zoom_w == 0) ? sensor_w : zoom_w;
  zoom_h = (zoom_h == 0) ? sensor_h : zoom_h;

  *magnification = ((((float) sensor_w / zoom_w) +
      ((float) sensor_h / zoom_h)) / 2) * 100;
}

bool UmdCamera::handleVideoControl(uint32_t id, uint32_t request,
  void* payload, void* userdata) {

  UmdCamera* ctx = static_cast<UmdCamera*>(userdata);
  CameraMetadata metadata;
  if (!ctx->GetCameraMetadata(metadata)) {
    return false;
  }

  UMD_LOG_INFO("Control: 0x%X, Request: 0x%X\n", id, request);

  switch (id) {
  case UMD_VIDEO_CTRL_BRIGHTNESS: {
    int16_t* value = (int16_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetExposureCompensation(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      ctx->GetExposureCompensation(metadata, value);
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.brightness_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.brightness_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.brightness_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_CONTRAST: {
    uint16_t* value = (uint16_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetContrast(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      ctx->GetContrast(metadata, value);
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.contrast_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.contrast_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.contrast_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_SATURATION: {
    uint16_t* value = (uint16_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetSaturation(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      ctx->GetSaturation(metadata, value);
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.saturation_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.saturation_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.saturation_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_SHARPNESS: {
    uint16_t* value = (uint16_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetSharpness(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      ctx->GetSharpness(metadata, value);
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.sharpness_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.sharpness_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.sharpness_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_BACKLIGHT_COMPENSATION: {
    uint16_t* value = (uint16_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetADRC(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      ctx->GetADRC(metadata, value);
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.backlight_comp_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.backlight_comp_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.backlight_comp_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_ANTIBANDING: {
    uint8_t* value = (uint8_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetAntibanding(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      if (!ctx->GetAntibanding(metadata, value)) {
        *value = ctx->mCtrlValues.antibanding_def;
        ctx->SetAntibanding(metadata, *value);
      }
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.antibanding_def;
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.antibanding_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.antibanding_max;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_GAIN: {
    uint16_t* value = (uint16_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetISO(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      ctx->GetISO(metadata, value);
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.gain_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.gain_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.gain_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_WB_TEMPERTURE: {
    uint16_t* value = (uint16_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetWbTemperature(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      if (!ctx->GetWbTemperature(metadata, value)) {
        *value = ctx->mCtrlValues.wb_temp_def;
        ctx->SetWbTemperature(metadata, *value);
      }
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.wb_temp_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.wb_temp_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.wb_temp_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_WB_MODE: {
    uint8_t* value = (uint8_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetWbMode(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      if (!ctx->GetWbMode(metadata, value)) {
        *value = ctx->mCtrlValues.wb_mode_def;
        ctx->SetWbMode(metadata, *value);
      }
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.wb_mode_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_EXPOSURE_TIME: {
    uint32_t* value = (uint32_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetExposureTime(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      ctx->GetExposureTime(metadata, value);
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.exp_time_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.exp_time_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.exp_time_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_EXPOSURE_MODE: {
    uint8_t* value = (uint8_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetExposureMode(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      if (!ctx->GetExposureMode(metadata, value)) {
        *value = ctx->mCtrlValues.exp_mode_def;
        ctx->SetExposureMode(metadata, *value);
      }
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.exp_mode_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_EXPOSURE_PRIORITY: {
    uint8_t* value = (uint8_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      if (*value == UMD_VIDEO_EXPOSURE_PRIORITY_CONSTANT) {
        UMD_LOG_ERROR("No Support for Exposure Priority\n");
      }
      break;
    case UMD_CTRL_GET_REQUEST:
      *value = UMD_VIDEO_EXPOSURE_PRIORITY_CONSTANT;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_FOCUS_MODE: {
    uint8_t* value = (uint8_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetFocusMode(metadata, *value);
      break;
    case UMD_CTRL_GET_REQUEST:
      if (!ctx->GetFocusMode(metadata, value)) {
        *value = ctx->mCtrlValues.exp_focus_mode_def;
        ctx->SetFocusMode(metadata, *value);
      }
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.exp_focus_mode_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_ZOOM: {
    uint16_t* value = (uint16_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetZoom(metadata, value, NULL, ctx->mCtrlValues);
      break;
    case UMD_CTRL_GET_REQUEST:
      ctx->GetZoom(metadata, value);
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.zoom_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.zoom_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.zoom_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  case UMD_VIDEO_CTRL_PANTILT: {
    uint64_t* value = (uint64_t*)payload;
    switch (request) {
    case UMD_CTRL_SET_REQUEST:
      ctx->SetZoom(metadata, NULL, value, ctx->mCtrlValues);
      break;
    case UMD_CTRL_GET_REQUEST:
      *value = umd_current_pan_and_tilt;
      break;
    case UMD_CTRL_MIN_REQUEST:
      *value = ctx->mCtrlValues.pan_tilt_min;
      break;
    case UMD_CTRL_MAX_REQUEST:
      *value = ctx->mCtrlValues.pan_tilt_max;
      break;
    case UMD_CTRL_DEF_REQUEST:
      *value = ctx->mCtrlValues.pan_tilt_def;
      break;
    default:
      UMD_LOG_ERROR("Unknown control request 0x%X!\n", request);
      return false;
    }
    break;
  }
  default:
    UMD_LOG_ERROR("Unknown control request 0x%X!\n", id);
    return false;
  }
  if (request == UMD_CTRL_SET_REQUEST) {
    if (!ctx->SetCameraMetadata(metadata)) {
      UMD_LOG_ERROR("Set camera metadata failed!\n");
      return false;
    }
  }
  return true;
}

void UmdCamera::ErrorCb(CameraErrorCode errorCode,
                           const CaptureResultExtras &extras) {
  UMD_LOG_ERROR("%s: ErrorCode: %d frameNumber %d requestId %d\n", __func__,
      errorCode, extras.frameNumber, extras.requestId);
}

void UmdCamera::IdleCb() {
  UMD_LOG_DEBUG("%s: Idle state notification\n", __func__);
}

void UmdCamera::ShutterCb(const CaptureResultExtras &, int64_t) {
  UMD_LOG_DEBUG("%s \n", __func__);
}

void UmdCamera::PreparedCb(int stream_id) {
  UMD_LOG_DEBUG("%s: Stream with id: %d prepared\n", __func__, stream_id);
}

void UmdCamera::ResultCb(const CaptureResult &result) {
  UMD_LOG_DEBUG("%s: Result requestId: %d partial count: %d\n", __func__,
      result.resultExtras.requestId, result.resultExtras.partialResultCount);
}

void UmdCamera::StreamCb(StreamBuffer buffer) {
  int maxsize = 0;
  int size = 0;
  uint8_t *mapped_buffer = nullptr;
  MemAllocFlags usage;
  MemAllocError ret;

  UMD_LATENCY_LOG("UmdCamera-latency: FrameNumber: %d Buffer from HAL\n",
      buffer.frame_number);
  if (mActive) {
    usage.flags = IMemAllocUsage::kSwReadOften;
    ret = mAllocDeviceInterface->MapBuffer(
                                     buffer.handle, 0,
                                     0, buffer.info.plane_info[0].width,
                                     buffer.info.plane_info[0].height,
                                     usage, (void **)&mapped_buffer);

    if ((MemAllocError::kAllocOk != ret) || (NULL == mapped_buffer)) {
      UMD_LOG_ERROR("%s: Unable to map buffer: %p res: %d\n", __func__,
          mapped_buffer, ret);
      goto fail_return;
    }

    switch (buffer.info.format) {
      case BufferFormat::kBLOB:
        maxsize = buffer.info.plane_info[0].size;
        size = GetBlobSize(mapped_buffer, buffer.info.plane_info[0].size);
        break;
      case BufferFormat::kYUY2:
        size = buffer.info.plane_info[0].stride * buffer.info.plane_info[0].height * 2;
        break;
#ifdef ENABLE_H264
      case BufferFormat::kNV12: {
        std::shared_ptr<C2Buffer> c2buffer;
        uint64_t timestamp = buffer.timestamp;
        uint32_t flags = 0;
        uint64_t index = buffer.frame_number;
        std::list<std::unique_ptr<C2Param>> settings;

        c2buffer = ImportGraphicBuffer(buffer);

        if (c2buffer == nullptr)
          UMD_LOG_ERROR ("Failed to create c2buffer\n");
        else
          mC2Module->Queue(c2buffer, settings, index, timestamp, flags);

        mAllocDeviceInterface->UnmapBuffer(buffer.handle);
        mDeviceClient->ReturnStreamBuffer(buffer);
        return;
      }
#endif
      default:
        UMD_LOG_ERROR("Unsupported format %d!\n", buffer.info.format);
        goto fail_unmap;
        break;
    }
    UMD_LATENCY_LOG("UmdCamera-latency: FrameNumber: %d fd: %d Submit " \
      "buffer to UMD \n", buffer.frame_number, buffer.fd);
    uint32_t bufidx = umd_gadget_submit_buffer (mGadget, UMD_VIDEO_STREAM_ID,
        mapped_buffer, size, maxsize, buffer.timestamp);
    if (bufidx < 0) {
      goto fail_unmap;
    }
    mVideoBufferQueue.push(std::make_pair (buffer, bufidx));
    return;
  }

fail_unmap:
  mAllocDeviceInterface->UnmapBuffer(buffer.handle);

fail_return:
  mDeviceClient->ReturnStreamBuffer(buffer);
}

void UmdCamera::cameraThreadHandler() {
  bool running = true;
  while (running) {
    UmdCameraMessage event;
    mMsg.pop(event);
    switch(event) {
      case UmdCameraMessage::CAMERA_START:
        UMD_LOG_DEBUG ("CAMERA_START\n");
        if (!CameraStart()) {
          UMD_LOG_ERROR ("Camera start failed.\n");
        }
        break;
      case UmdCameraMessage::CAMERA_STOP:
        UMD_LOG_DEBUG ("CAMERA_STOP\n");
        if (!CameraStop()) {
          UMD_LOG_ERROR ("Camera stop failed.\n");
        }
        break;
      case UmdCameraMessage::CAMERA_SUBMIT_REQUEST:
        UMD_LOG_DEBUG ("CAMERA_SUBMIT_REQUEST\n");
        if (!CameraSubmitRequest()) {
          UMD_LOG_ERROR ("Camera submit request failed.\n");
        }
        break;
      case UmdCameraMessage::CAMERA_TERMINATE:
        UMD_LOG_DEBUG ("CAMERA_TERMINATE\n");
        if (!CameraStop()) {
          UMD_LOG_ERROR("Camera stop failed.\n");
        }
        running = false;
        break;
      default:
        UMD_LOG_ERROR("Unknown event type: %d", event);
    }
  }
}

void UmdCamera::videoBufferLoop() {
  while (mActive || mVideoBufferQueue.size()) {
    std::pair<StreamBuffer, int32_t> buffer_pair;
    if (!mVideoBufferQueue.pop(buffer_pair)) {
      StreamBuffer buffer = buffer_pair.first;
      int32_t bufidx = buffer_pair.second;
      umd_gadget_wait_buffer (mGadget, UMD_VIDEO_STREAM_ID, bufidx);
      UMD_LATENCY_LOG ("UmdCamera-latency: FrameNumber: %d fd: %d Return buffer" \
          " from UMD \n", buffer.frame_number, buffer.fd);
      if (buffer.handle == nullptr) {
        UMD_LOG_ERROR("Invalid buffer handle\n");
        continue;
      }

      mAllocDeviceInterface->UnmapBuffer(buffer.handle);
      mDeviceClient->ReturnStreamBuffer(buffer);
    } else {
      UMD_LOG_ERROR("Video buffer timeout!\n");
    }
  }

  UMD_LOG_INFO("videoBufferLoop terminate!\n");
}

void UmdCamera::codecVideoBufferLoop() {
  while (mActive || mCodecVideoBufferQueue.size()) {
    int32_t bufidx;

    if (!mCodecVideoBufferQueue.pop(bufidx))
      umd_gadget_wait_buffer (mGadget, UMD_VIDEO_STREAM_ID, bufidx);
  }

  UMD_LOG_INFO("codecVideoBufferLoop terminate!\n");
}

bool UmdCamera::CameraStart() {
  UMD_LOG_DEBUG ("Camera start\n");
  mTv = {0, 0};
  mPrevtv = {0, 0};
  mCount = 0;
  const std::lock_guard<std::mutex> lock(mCameraMutex);

  CameraStreamParameters params = {};

  mVideoBufferThread = std::unique_ptr<std::thread>(
      new std::thread(&UmdCamera::videoBufferLoop, this));

  if (nullptr == mVideoBufferThread) {
    UMD_LOG_ERROR ("Video buffer thread creation failed!\n");
    return -ENOMEM;
  }

  if (mVsetup.width == 0 || mVsetup.height == 0) {
    UMD_LOG_ERROR ("Invalid stream resolution: %dx%d!\n",
        mVsetup.width, mVsetup.height == 0);
    return false;
  }

  auto ret = mDeviceClient->BeginConfigure();
  if (0 != ret) {
    UMD_LOG_ERROR ("Camera BeginConfigure failed!\n");
    return false;
  }

  params.bufferCount = STREAM_BUFFER_COUNT;
  params.rotation = mRotation;

  switch (mVsetup.format) {
    case UMD_VIDEO_FMT_YUYV:
      params.format = PixelFormat::YCBCR_422_I;
      break;
    case UMD_VIDEO_FMT_MJPEG:
      params.format = PixelFormat::BLOB;
      break;
#ifdef ENABLE_H264
    case UMD_VIDEO_FMT_H264:
      params.format = PixelFormat::IMPLEMENTATION_DEFINED;
      if (!InitializeCodec())
        return false;
      break;
#endif
    default:
      UMD_LOG_ERROR ("Unsupported video format: %d!\n", mVsetup.format);
      return false;
      break;
  }

  params.width = mVsetup.width;
  params.height = mVsetup.height;
  params.allocFlags.flags = IMemAllocUsage::kSwReadOften |
                            IMemAllocUsage::kHwCameraWrite;
  params.cb = [&](StreamBuffer buffer) { StreamCb(buffer); };

  mStreamId = mDeviceClient->CreateStream(params);
  if (mStreamId < 0) {
    UMD_LOG_ERROR("Camera CreateStream failed!\n");
    return false;
  }

  mRequest.streamIds.add(mStreamId);

  ret = mDeviceClient->EndConfigure();
  if (0 != ret) {
    UMD_LOG_ERROR ("Camera EndConfigure failed!\n");
    return false;
  }

  InitCameraParamsLocked();

  if (!CameraSubmitRequestLocked()) {
    UMD_LOG_ERROR ("SubmitRequest failed!\n");
    return false;
  }

  return true;
}

bool UmdCamera::CameraStop() {
  UMD_LOG_DEBUG ("Camera stop\n");

  const std::lock_guard<std::mutex> lock(mCameraMutex);

  if (mVideoBufferThread == nullptr) {
    UMD_LOG_ERROR ("Video loop thread not started!\n");
    return -EINVAL;
  }

  auto ret = mDeviceClient->CancelRequest(mRequestId, &mLastFrameNumber);
  if (0 != ret) {
    UMD_LOG_ERROR ("Camera CancelRequest failed!\n");
  }

  UMD_LOG_INFO("%s: Preview request cancelled last frame number: %" PRId64 "\n",
      __func__, mLastFrameNumber);

  ret = mDeviceClient->WaitUntilIdle();
  if (0 != ret) {
    UMD_LOG_ERROR ("Camera WaitUntilIdle failed!\n");
  }

  mVideoBufferQueue.abort();

  mVideoBufferThread->join();
  mVideoBufferThread = nullptr;

  for (uint32_t i = 0; i < mRequest.streamIds.size(); i++) {
    ret = mDeviceClient->DeleteStream(mRequest.streamIds[i], false);
    if (0 != ret) {
      UMD_LOG_ERROR ("Camera DeleteStream failed!\n");
    }
  }
  mRequest.streamIds.clear();
  mStreamId = -1;

  if (mCodecVideoBufferThread) {
    mCodecVideoBufferQueue.abort();
    mCodecVideoBufferThread->join();
    mCodecVideoBufferThread = nullptr;
  }
#ifdef ENABLE_H264
  // Stop codec2 module
  if (mC2Module) {
    mC2Module->Stop();
    delete mC2Module;
    mC2Module = nullptr;
  }
#endif

  mVideoBufferQueue.reset();
  mCodecVideoBufferQueue.reset();
  return true;
}

bool UmdCamera::CameraSubmitRequest() {
  UMD_LOG_DEBUG ("Camera submit request\n");

  const std::lock_guard<std::mutex> lock(mCameraMutex);

  return CameraSubmitRequestLocked();
}

bool UmdCamera::CameraSubmitRequestLocked() {
  mRequestId = mDeviceClient->SubmitRequest(mRequest, true, &mLastFrameNumber);
  if (0 > mRequestId) {
    UMD_LOG_ERROR ("Camera SubmitRequest failed!\n");
    return false;
  }
  return true;
}

bool UmdCamera::GetCameraMetadata(CameraMetadata &meta) {
  const std::lock_guard<std::mutex> lock(mCameraMutex);
  return GetCameraMetadataLocked(meta);
}

bool UmdCamera::GetCameraMetadataLocked(CameraMetadata &meta) {
  meta.clear();
  meta.append(mRequest.metadata);
  return true;
}

bool UmdCamera::SetCameraMetadata(CameraMetadata &meta, bool doSubmitReq) {
  const std::lock_guard<std::mutex> lock(mCameraMutex);
  return SetCameraMetadataLocked(meta, doSubmitReq);
}

bool UmdCamera::SetCameraMetadataLocked(CameraMetadata &meta, bool doSubmitReq) {
  mRequest.metadata.clear();
  mRequest.metadata.append(meta);
  if (doSubmitReq) {
    mMsg.push(UmdCameraMessage::CAMERA_SUBMIT_REQUEST);
  }
  return true;
}

uint32_t UmdCamera::GetBlobSize(uint8_t *buffer, uint32_t size) {
  uint32_t bsize = sizeof(struct camera3_jpeg_blob);
  uint32_t res = size;

  if (size > bsize) {
    uint8_t *footer = buffer + size - bsize - JPEG_BLOB_OFFSET;
    struct camera3_jpeg_blob *blob = (struct camera3_jpeg_blob *) footer;

    if (CAMERA3_JPEG_BLOB_ID == blob->jpeg_blob_id) {
      res = blob->jpeg_size;
    } else {
      UMD_LOG_ERROR("%s Invalid blob structure!\n", __func__);
    }
  } else {
    UMD_LOG_ERROR("%s Invalid blob size: %u\n", __func__, bsize);
  }

  return res;
}

void UmdCamera::SetDefaultControlValues(CameraMetadata& meta) {

  {
    uint32_t tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.iso_exp_priority", "select_priority");
    if (tag != 0) {
      // Here priority is CamX ISOPriority whose index is 0.
      int32_t priority = 0;
      meta.update(tag, &priority, 1);
    }

    tag = GetVendorTagByName(
      "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_exp_priority");
    if (tag != 0) {
      int64_t isomode = 0; // QCAMERA3_ISO_MODE_AUTO
      meta.update(tag, &isomode, 1);
    }
  }

  SetExposureCompensation(meta, mCtrlValues.brightness_def);
  SetContrast(meta, mCtrlValues.contrast_def);
  SetSaturation(meta, mCtrlValues.saturation_def);
  SetSharpness(meta, mCtrlValues.sharpness_def);
  SetAntibanding(meta, mCtrlValues.antibanding_def);
  SetADRC(meta, mCtrlValues.backlight_comp_def);
  SetISO(meta, mCtrlValues.gain_def);
  SetWbTemperature(meta, mCtrlValues.wb_temp_def);
  SetWbMode(meta, mCtrlValues.wb_mode_def);
  SetExposureTime(meta, mCtrlValues.exp_time_def);
  SetExposureMode(meta, mCtrlValues.exp_mode_def);
  SetFocusMode(meta, mCtrlValues.exp_focus_mode_def);
  SetZoom(meta, &mCtrlValues.zoom_def, &mCtrlValues.pan_tilt_def, mCtrlValues);
}

void UmdCamera::FillInitialControlValue() {
  int32_t pan_min, pan_max, pan_def, tilt_min, tilt_max, tilt_def;
  mCtrlValues.brightness_min = -12;
  mCtrlValues.brightness_max = 12;
  mCtrlValues.brightness_def = 0;

  mCtrlValues.contrast_min = 1;
  mCtrlValues.contrast_max = 10;
  mCtrlValues.contrast_def = 5;

  mCtrlValues.saturation_min = 0;
  mCtrlValues.saturation_max = 10;
  mCtrlValues.saturation_def = 5;

  mCtrlValues.sharpness_min = 0;
  mCtrlValues.sharpness_max = 6;
  mCtrlValues.sharpness_def = 2;

  mCtrlValues.antibanding_def = UMD_VIDEO_ANTIBANDING_AUTO;
  mCtrlValues.antibanding_min = UMD_VIDEO_ANTIBANDING_DISABLED;
  mCtrlValues.antibanding_max = UMD_VIDEO_ANTIBANDING_AUTO;

  mCtrlValues.backlight_comp_min = 0;
  mCtrlValues.backlight_comp_max = 1;
  mCtrlValues.backlight_comp_def = 0;

  mCtrlValues.gain_min = 100;
  mCtrlValues.gain_max = 3200;
  mCtrlValues.gain_def = 800;

  mCtrlValues.wb_temp_min = 2800;
  mCtrlValues.wb_temp_max = 6500;
  mCtrlValues.wb_temp_def = 4600;

  mCtrlValues.wb_mode_def = UMD_VIDEO_WB_MODE_AUTO;

  mCtrlValues.exp_time_min = 333;
  mCtrlValues.exp_time_max = 100000;
  mCtrlValues.exp_time_def = 333;

  mCtrlValues.exp_mode_def = UMD_VIDEO_EXPOSURE_MODE_AUTO;

  mCtrlValues.exp_focus_mode_def = UMD_VIDEO_FOCUS_MODE_AUTO;

  mCtrlValues.zoom_min = 100;
  mCtrlValues.zoom_max = 500;
  mCtrlValues.zoom_def = 100;

  pan_min = -25;
  pan_max = 25;
  pan_def = 0;
  tilt_min = -25;
  tilt_max = 25;
  tilt_def = 0;

  mCtrlValues.pan_tilt_min = UMD_VIDEO_CTRL_SET_PAN_AND_TILT(pan_min, tilt_min);
  mCtrlValues.pan_tilt_max = UMD_VIDEO_CTRL_SET_PAN_AND_TILT(pan_max, tilt_max);
  mCtrlValues.pan_tilt_def = UMD_VIDEO_CTRL_SET_PAN_AND_TILT(pan_def, tilt_def);
}

#ifdef ENABLE_H264
bool UmdCamera::InitializeCodec() {
  // Initialize codec2 component
  mC2Module = C2Factory::GetModule(C2_COMPONENT_NAME, kVideoEncode);
  if (nullptr == mC2Module) {
    UMD_LOG_ERROR ("Failed to create c2module\n");
    return false;
  }

  UmdFrameCallback umdFrameCb = [&](uint8_t* data, uint32_t size, uint64_t
    timestamp) {
    uint32_t bufidx = umd_gadget_submit_buffer (mGadget, UMD_VIDEO_STREAM_ID,
        data, size, size, timestamp);
    PrintFPS();
    mCodecVideoBufferQueue.push(bufidx); };

  std::shared_ptr<IC2Notifier> notifier = std::make_shared<UmdC2Notifier>(
    umdFrameCb);
  mC2Module->Initialize(notifier);

  // Set the encoder parameters
  SetEncoderParameters();

  // Start c2 component
  if (mC2Module->Start()) {
    UMD_LOG_ERROR ("Failed to start c2module\n");
    delete mC2Module;
    return false;
  }

  mCodecVideoBufferThread = std::unique_ptr<std::thread>(
      new std::thread(&UmdCamera::codecVideoBufferLoop, this));

  if (nullptr == mCodecVideoBufferThread) {
    UMD_LOG_ERROR ("Codec video buffer thread creation failed!\n");
    delete mC2Module;
    return false;
  }

  return true;
}

void UmdCamera::SetParams (std::unique_ptr<C2Param> c2param, std::string type) {
  try {
    mC2Module->SetParam(c2param);
    UMD_LOG_INFO ("Successfully set parameter: %s", type.c_str());
  } catch (std::exception& e) {
    UMD_LOG_ERROR ("Failed to set c2module parameter, error: '%s'!", e.what());
  }
}

void UmdCamera::SetEncoderParameters() {
  std::unique_ptr<C2Param> c2param;

  // input format
  C2StreamPixelFormatInfo::input pixformat;
  pixformat.value = static_cast<uint32_t>(C2PixelFormat::kNV12);
  SetParams(C2Param::Copy(pixformat), C2_PARAMKEY_PIXEL_FORMAT);

  // input resolution
  C2StreamPictureSizeInfo::input dimensions;
  dimensions.width = mVsetup.width;
  dimensions.height = mVsetup.height;
  SetParams(C2Param::Copy(dimensions), C2_PARAMKEY_PICTURE_SIZE);

  // output framerate
  C2StreamFrameRateInfo::output framerate;
  framerate.value = C2_OUT_FRAMERATE;
  SetParams(C2Param::Copy(framerate), C2_PARAMKEY_FRAME_RATE);

  // profile level
  C2StreamProfileLevelInfo::output plinfo;
  plinfo.profile = C2Config::profile_t::PROFILE_AVC_HIGH;
  SetParams(C2Param::Copy(plinfo), C2_PARAMKEY_PROFILE_LEVEL);

  // rate control
  C2StreamBitrateModeTuning::output ratectrl;
  ratectrl.value = static_cast<C2Config::bitrate_mode_t>(C2_RATE_CTRL_DISABLE);
  SetParams(C2Param::Copy(ratectrl), C2_PARAMKEY_BITRATE_MODE);

  // bitrate
  C2StreamBitrateInfo::output bitrate;
  bitrate.value = C2_BITRATE;
  SetParams(C2Param::Copy(bitrate), C2_PARAMKEY_BITRATE);

  // gop
  auto c2gop = C2StreamGopTuning::output::AllocUnique(2, 0u);
  c2gop->m.values[0] = {P_FRAME, C2_PFRAME_VALUE};
  c2gop->m.values[1] =
      {C2Config::picture_type_t(P_FRAME | B_FRAME), C2_BFRAME_VALUE};
  SetParams(C2Param::Copy(*c2gop), C2_PARAMKEY_GOP);

  // prepend header
  C2PrependHeaderModeSetting csdmode;
  csdmode.value = PREPEND_HEADER_TO_ALL_SYNC;
  SetParams(C2Param::Copy(csdmode), C2_PARAMKEY_PREPEND_HEADER_MODE);

  // intra refresh
  C2StreamIntraRefreshTuning::output irefresh;
  irefresh.mode = C2Config::INTRA_REFRESH_DISABLED;
  irefresh.period = C2_REFRESH_PERIOD;
  SetParams(C2Param::Copy(irefresh), C2_PARAMKEY_INTRA_REFRESH);
}
#endif

void UmdCamera::PrintFPS() {
  clock_gettime(CLOCK_MONOTONIC, &mTv);
  uint64_t time_diff = (uint64_t)((mTv.tv_sec * 1000000 + mTv.tv_nsec / 1000) -
      (mPrevtv.tv_sec * 1000000 + mPrevtv.tv_nsec / 1000));
  mCount++;
  if (time_diff >= FPS_TIME_INTERVAL) {
    bool is_first_time = (mPrevtv.tv_sec == 0 && mPrevtv.tv_nsec == 0);
    if (!is_first_time) {
      float framerate = (mCount * 1000000) / (float)time_diff;
      UMD_LOG_INFO("Encoded FPS = %0.2f", framerate);
    }
    mPrevtv = mTv;
    mCount = 0;
  }
}
