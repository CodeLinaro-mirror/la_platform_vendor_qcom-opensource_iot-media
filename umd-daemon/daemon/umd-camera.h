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

/*
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <umd-gadget.h>

#include <camera_device_client.h>
#include <camera_utils.h>

#include <string>
#include <thread>
#include <memory>

#include "message_queue.h"

#ifdef ENABLE_H264
#include "c2-module.h"
#endif

#include "umd-video-data-processing.h"

using namespace ::android;
using namespace ::camera::adaptor;
using namespace ::camera;

typedef std::function<void(uint8_t* data, uint32_t size, uint64_t timestamp, StreamBuffer &buffer)>
  UmdFrameCallback;
typedef std::function<void(uint64_t frameNumber)>UmdFrameDropCallback;

enum
{
  PARTIAL_MWB_MODE_DISABLE = 0,
  PARTIAL_MWB_MODE_CCT,
  PARTIAL_MWB_MODE_GAINS
};

enum class UmdCameraMessage {
  CAMERA_START,
  CAMERA_STOP,
  CAMERA_SUBMIT_REQUEST,
  CAMERA_TERMINATE
};

struct UVCControlValues {
  int16_t   brightness_min;
  int16_t   brightness_max;
  int16_t   brightness_def;
  uint16_t  contrast_min;
  uint16_t  contrast_max;
  uint16_t  contrast_def;
  uint16_t  saturation_min;
  uint16_t  saturation_max;
  uint16_t  saturation_def;
  uint16_t  sharpness_min;
  uint16_t  sharpness_max;
  uint16_t  sharpness_def;
  uint8_t   antibanding_def;
  uint8_t   antibanding_min;
  uint8_t   antibanding_max;
  uint16_t  backlight_comp_min;
  uint16_t  backlight_comp_max;
  uint16_t  backlight_comp_def;
  uint16_t  gain_min;
  uint16_t  gain_max;
  uint16_t  gain_def;
  uint16_t  wb_temp_min;
  uint16_t  wb_temp_max;
  uint16_t  wb_temp_def;
  uint8_t   wb_mode_def;
  uint32_t  exp_time_min;
  uint32_t  exp_time_max;
  uint32_t  exp_time_def;
  uint8_t   exp_mode_def;
  uint8_t   exp_focus_mode_def;
  uint16_t  zoom_min;
  uint16_t  zoom_max;
  uint16_t  zoom_def;
  uint64_t  pan_tilt_min;
  uint64_t  pan_tilt_max;
  uint64_t  pan_tilt_def;
};

class UmdCamera : public RefBase {
public:
  ~UmdCamera();
  UmdCamera(std::string uvcdev, int cameraId);
  int32_t StartUVC();
  void StopUVC();

private:
  static bool setupVideoStream(UmdVideoSetup * stmsetup, void * userdata);
  static bool enableVideoStream(void * userdata);
  static bool disableVideoStream(void * userdata);
  static bool handleVideoControl(uint32_t id, uint32_t request, void * payload,
                                 void * userdata);

  static uint64_t umd_current_pan_and_tilt;

  uint32_t GetVendorTagByName (const char * section, const char * name);

  bool InitCameraParamsLocked();

  void SetExposureCompensation (CameraMetadata & meta, int16_t value);
  void GetExposureCompensation (CameraMetadata & meta, int16_t * value);
  void SetContrast (CameraMetadata & meta, uint16_t value);
  void GetContrast (CameraMetadata & meta, uint16_t * value);
  void SetSaturation (CameraMetadata & meta, uint16_t value);
  void GetSaturation (CameraMetadata & meta, uint16_t * value);
  void SetSharpness (CameraMetadata & meta, uint16_t value);
  void GetSharpness (CameraMetadata & meta, uint16_t * value);
  void SetADRC (CameraMetadata & meta, uint16_t value);
  void GetADRC (CameraMetadata & meta, uint16_t * value);
  void SetAntibanding (CameraMetadata & meta, uint8_t value);
  bool GetAntibanding (CameraMetadata & meta, uint8_t * value);
  void SetISO (CameraMetadata & meta, uint16_t value);
  void GetISO (CameraMetadata & meta, uint16_t * value);
  void SetWbTemperature (CameraMetadata & meta, uint16_t value);
  bool GetWbTemperature (CameraMetadata & meta, uint16_t * value);
  void SetWbMode (CameraMetadata & meta, uint8_t value);
  bool GetWbMode (CameraMetadata & meta, uint8_t * value);
  void SetExposureTime (CameraMetadata & meta, uint32_t value);
  void GetExposureTime (CameraMetadata & meta, uint32_t * value);
  void SetExposureMode (CameraMetadata & meta, uint8_t value);
  bool GetExposureMode (CameraMetadata & meta, uint8_t * value);
  void SetFocusMode (CameraMetadata & meta, uint8_t value);
  bool GetFocusMode (CameraMetadata & meta, uint8_t * value);
  void SetZoom(CameraMetadata & meta, uint16_t *magnification,
      uint64_t *pan_and_tilt, UVCControlValues &ctrl_vals);
  void GetZoom(CameraMetadata & meta, uint16_t * magnification);

  void ErrorCb(CameraErrorCode errorCode,
                           const CaptureResultExtras &extras);
  void IdleCb();
  void ShutterCb(const CaptureResultExtras &, int64_t);
  void PreparedCb(int stream_id);
  void ResultCb(const CaptureResult &result);
  void StreamCb(StreamBuffer buffer);

  int32_t InitializeAudio();

  void cameraThreadHandler();

  int32_t InitializeCamera();
  bool CameraStart();
  bool CameraStop();
  bool CameraSubmitRequest();
  bool CameraSubmitRequestLocked();
  bool GetCameraMetadata(CameraMetadata &meta);
  bool GetCameraMetadataLocked(CameraMetadata &meta);
  bool SetCameraMetadata(CameraMetadata &meta, bool doSubmitReq = true);
  bool SetCameraMetadataLocked(CameraMetadata &meta, bool doSubmitReq = true);
  void SetDefaultControlValues(CameraMetadata& meta);
  void FillInitialControlValue();

  uint32_t GetGadgetCount();

  UmdGadget *mGadget;
  UmdVideoSetup mVsetup;
  UmdVideoCallbacks mUmdVideoCallbacks;
  UmdVideoData *mVdata;
  std::mutex mGadgetMutex;
  std::string mUvcDev;
  std::string mUacDev;
  std::string mMicDev;
  std::string mSpeakerDev;

  std::unique_ptr<std::thread> mCameraThread;
  MessageQ<UmdCameraMessage> mMsg;

  int mCameraId;
  int mStreamId;
  std::atomic<bool> mActive;
  bool mOnlyUAC;

  std::shared_ptr<Camera3DeviceClient> mDeviceClient;
  CameraMetadata mStaticInfo;
  CameraClientCallbacks mClientCb;
  Camera3Request mRequest;

  int64_t mLastFrameNumber;
  int32_t mRequestId;

  std::mutex mCameraMutex;

  UVCControlValues mCtrlValues;
  StreamRotation mRotation;

  int32_t mStreamIdx;
  static uint32_t mGadgetItr;
  static bool mCamStartDone;
  static bool mCamStopDone;
  std::vector<int> mCameraStreams;
  std::vector<UmdGadget *> mGadgets;
  std::vector<C2Module *> mC2Modules;
  uint32_t mGadgetCount;
  std::unordered_map<int, std::pair<UmdGadget *, UmdVideoData *>> mStreamMap;
};
