/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 */

/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "CameraAdaptor"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <string.h>
#include "utils/camera_log.h"
#include "camera_utils.h"
#include "camera_device_client.h"
#include <aidl/android/hardware/camera/provider/ICameraProvider.h>
#include "camera_hidl_vendor_tag_descriptor.h"
#include <android/binder_manager.h>
#include <android/binder_process.h>

#define QCAMERA3_SENSORMODE_ZZHDR_OPMODE      (0xF002)
#define QCAMERA3_SENSORMODE_FPS_DEFAULT_INDEX (0x0)
#define FORCE_SENSORMODE_ENABLE               (1 << 24)
#define EIS_ENABLE                            (0xF200)
#define LDC_ENABLE                            (0xF800)
#define LCAC_ENABLE                           (0x100000)

// Convenience macros for transitioning to the error state
#define SET_ERR(fmt, ...) \
  SetErrorState("%s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#define SET_ERR_L(fmt, ...) \
  SetErrorStateLocked("%s: " fmt, __FUNCTION__, ##__VA_ARGS__)
using namespace qcamera;

using ::aidl::android::hardware::camera::provider::ICameraProvider;

uint32_t camera_log_level;

namespace camera {

namespace adaptor {

std::mutex Camera3DeviceClient::vendor_tag_mutex_;
sp<VendorTagDescriptor> Camera3DeviceClient::vendor_tag_desc_ = nullptr;
uint32_t Camera3DeviceClient::client_count_ = 0;

Camera3DeviceClient::Camera3DeviceClient(CameraClientCallbacks clientCb)
    : client_cb_(clientCb),
      id_(0),
      state_(STATE_NOT_INITIALIZED),
      flush_on_going_(false),
      next_stream_id_(0),
      reconfig_(false),
      camera_provider_(nullptr),
      camera_device_(nullptr),
      camera_session_(nullptr),
      number_of_cameras_(0),
      alloc_device_interface_(NULL),
      next_request_id_(0),
      frame_number_(0),
      next_shutter_frame_number_(0),
      next_shutter_input_frame_number_(0),
      partial_result_count_(0),
      is_partial_result_supported_(false),
      next_result_frame_number_(0),
      next_result_input_frame_number_(0),
      monitor_(),
      request_handler_(monitor_),
      pause_state_notify_(false),
      state_listeners_(0),
      is_hfr_supported_(false),
      is_raw_only_(false),
      hfr_mode_enabled_(false),
      cam_feature_flags_(static_cast<uint32_t>(CamFeatureFlag::kNone)),
      fps_sensormode_index_(0),
      prepare_handler_(),
      input_stream_{} {
  CAMERA_GET_LOG_LEVEL();
  pthread_mutex_init(&lock_, NULL);
  pthread_mutex_init(&pending_requests_lock_, NULL);
  cond_init(&state_updated_);
  input_stream_.stream_id = -1;
  prepare_handler_.SetPrepareCb(clientCb.peparedCb);
}

Camera3DeviceClient::~Camera3DeviceClient() {
  request_handler_.RequestExit();

  prepare_handler_.Clear();
  prepare_handler_.RequestExit();

  for (uint32_t i = 0; i < streams_.size(); i++) {
    Camera3Stream *stream = streams_.editValueAt(i);
    delete stream;
  }
  streams_.clear();

  std::vector<Camera3Stream* >::iterator it = deleted_streams_.begin();
  while (it != deleted_streams_.end()) {
    Camera3Stream *stream = *it;
    it = deleted_streams_.erase(it);
    delete stream;

  }
  deleted_streams_.clear();

  monitor_.RequestExitAndWait();

  if (nullptr != alloc_device_interface_) {
    AllocDeviceFactory::DestroyAllocDevice(alloc_device_interface_);
    alloc_device_interface_ = nullptr;
  }

  pending_error_requests_vector_.clear();

  pthread_cond_destroy(&state_updated_);
  pthread_mutex_destroy(&pending_requests_lock_);
  pthread_mutex_destroy(&lock_);

  if (camera_session_) {
    camera_session_->close();
  }
}

int32_t Camera3DeviceClient::Initialize() {
  int32_t res = 0;

  pthread_mutex_lock(&lock_);

  if (state_ != STATE_NOT_INITIALIZED) {
    CAMERA_ERROR("%s: Already initialized! \n", __func__);
    res = -ENOSYS;
    goto exit;
  }

  {
    bool success = ABinderProcess_setThreadPoolMaxThreadCount(5);
    CAMERA_DEBUG("ABinderProcess_setThreadPoolMaxThreadCount returns %s", success ? "true" : "false");

    ABinderProcess_startThreadPool();

    std::string serviceDescriptor = std::string() + ICameraProvider::descriptor + "/vendor_qti/0";
    ndk::SpAIBinder cameraProviderBinder = SpAIBinder(AServiceManager_getService(serviceDescriptor.c_str()));
    if(cameraProviderBinder.get() == nullptr){
      CAMERA_ERROR("%s: Failed to get camera provider service \n",__func__);
    }
    camera_provider_ = ICameraProvider::fromBinder(cameraProviderBinder);
  }

  if (camera_provider_ == nullptr) {
    CAMERA_ERROR("%s: Invalid camera provider \n", __func__);
    res = -ENOSYS;
    goto exit;
  }

  ret_ = camera_provider_->getCameraIdList(&camera_device_names_);
  if (!ret_.isOk()) {
    CAMERA_ERROR("%s: Could not get camera id list \n");
  }

  number_of_cameras_ = camera_device_names_.size();
  CAMERA_INFO("%s: Number of cameras: %d\n", __func__, number_of_cameras_);

  {
    std::lock_guard<std::mutex> lk(vendor_tag_mutex_);
    if (client_count_ == 0) {
      std::vector<VendorTagSection> vt_sections;
      ret_ = camera_provider_->getVendorTags(&vt_sections);
      if (!ret_.isOk()) {
        CAMERA_ERROR("%s: Error getting vendor tags from provider \n",__func__);
        goto exit;
      }

      res = CustomVendorTagDescriptor::createDescriptorFromHidl(
          vt_sections, vendor_tag_desc_);

      if (0 != res) {
        CAMERA_ERROR("%s: Could not generate descriptor from HIDL,"
            "received error %s (%d). Camera clients will not be able to use"
            "vendor tags", __FUNCTION__, strerror(res), res);
        goto exit;
      }

      // Set the global descriptor to use with camera metadata
      res = VendorTagDescriptor::setAsGlobalVendorTagDescriptor(vendor_tag_desc_);

      if (0 != res) {
        CAMERA_ERROR(
            "%s: Could not set vendor tag descriptor, "
            "received error %s (%d). \n",
            __func__, strerror(-res), res);
        goto exit;
      }
    }
    ++client_count_;
  }

  alloc_device_interface_ = AllocDeviceFactory::CreateAllocDevice();
  state_ = STATE_CLOSED;
  next_stream_id_ = 0;
  reconfig_ = true;

  pthread_mutex_unlock(&lock_);

  return res;

exit:

  if (nullptr != alloc_device_interface_) {
    AllocDeviceFactory::DestroyAllocDevice(alloc_device_interface_);
    alloc_device_interface_ = nullptr;
  }

  {
    std::lock_guard<std::mutex> lk(vendor_tag_mutex_);
    if (client_count_ == 0) {
      VendorTagDescriptor::clearGlobalVendorTagDescriptor();
    }
  }

  camera_provider_ = nullptr;
  camera_device_names_.clear();

  pthread_mutex_unlock(&lock_);

  return res;
}

int32_t Camera3DeviceClient::OpenCamera(uint32_t idx) {

  int32_t res = 0;
  std::string id;
  std::string name;
  camera_metadata_entry_t capsEntry;
  MarkRequest mark_cb = [&] (uint32_t frameNumber, int32_t numBuffers,
                                 CaptureResultExtras resultExtras) {
    return MarkPendingRequest(frameNumber,numBuffers, resultExtras); };
  SetError set_error = [&] (const char *fmt, va_list args) {
    SetErrorStateV(fmt, args);
  };
  if (idx >= number_of_cameras_) {
    CAMERA_ERROR("%s: Invalid camera idx: %d\n", __func__, idx);
    return -EINVAL;
  }

  if (nullptr == camera_provider_.get()) {
    CAMERA_ERROR("%s: Camera provider is not initialized!\n", __func__);
    return -ENODEV;
  }

  if (nullptr != camera_device_.get()) {
    CAMERA_ERROR("%s: Camera device is already open!\n", __func__);
    return -EINVAL;
  }

  pthread_mutex_lock(&lock_);

  if (state_ != STATE_CLOSED) {
    CAMERA_ERROR("%s: Invalid state: %d! \n", __func__, state_);
    res = -ENOSYS;
    goto exit;
  }

  ret_ = camera_provider_->getCameraDeviceInterface(camera_device_names_[idx], &camera_device_);

  CAMERA_INFO("getCameraDeviceInterface returns status:%d:%d", ret_.getExceptionCode(),
          ret_.getServiceSpecificError());

  cameraClientCallback_.reset(this);
  ret_ = camera_device_->open(cameraClientCallback_ , &camera_session_);
  if (!ret_.isOk()) {
    CAMERA_ERROR("%s : Open Camera failed",__func__);
  }

  {
    ::aidl::android::hardware::camera::device::CameraMetadata cameraCharacteristics;
    ret_ = camera_device_->getCameraCharacteristics(&cameraCharacteristics);
    if (!ret_.isOk()) {
      CAMERA_INFO("%s :Failed to get camera characteristics for device",__func__);
    }
    auto camera_metadata = reinterpret_cast<camera_metadata_t*>(cameraCharacteristics.metadata.data());
    device_info_.clear();
    device_info_.append(camera_metadata);
  }

  {
    ::aidl::android::hardware::common::fmq::MQDescriptor<
                  int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>
                  descriptor;
    ndk::ScopedAStatus resultQueueRet = camera_session_->getCaptureResultMetadataQueue(&descriptor);
    if (!resultQueueRet.isOk()) {
      CAMERA_INFO("Failed to get Capture Result Metadata Queue for device");
    }

    result_metadata_queue_ = std::make_shared<ResultMetadataQueue>(descriptor);
    if (!result_metadata_queue_->isValid() || result_metadata_queue_->availableToWrite() <= 0) {
          CAMERA_ERROR("%s: getCaptureResultMetadataQueue failed", __func__);
          result_metadata_queue_ = nullptr;
        }
  }

  {
    camera_metadata_entry partialResultsCount =
        device_info_.find(ANDROID_REQUEST_PARTIAL_RESULT_COUNT);
    if (partialResultsCount.count > 0) {
      partial_result_count_ = partialResultsCount.data.i32[0];
      is_partial_result_supported_ = (partial_result_count_ > 1);
    }
  }

  capsEntry = device_info_.find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
  for (uint32_t i = 0; i < capsEntry.count; ++i) {
    uint8_t caps = capsEntry.data.u8[i];
    if (ANDROID_REQUEST_AVAILABLE_CAPABILITIES_CONSTRAINED_HIGH_SPEED_VIDEO ==
        caps) {
      is_hfr_supported_ = true;
      break;
    }
  }

  id_ = idx;
  state_ = STATE_NOT_CONFIGURED;

  name = "C3-" + std::to_string(idx) + "-Monitor";

  monitor_.SetIdleNotifyCb([&] (bool idle) {NotifyStatus(idle);});
  monitor_.Run(name);
  if (0 != res) {
    SET_ERR_L("Unable to start monitor: %s (%d)", strerror(-res), res);
    goto exit;
  }

  name = "C3-" + std::to_string(idx) + "-Handler";

  request_handler_.Initialize(camera_session_, client_cb_.errorCb, mark_cb, set_error);
  res = request_handler_.Run(name);
  if (0 > res) {
    SET_ERR_L("Unable to start request handler: %s (%d)", strerror(-res), res);
    goto exit;
  }

  pthread_mutex_unlock(&lock_);

  return res;

exit:
  if (camera_session_) {
    camera_session_->close();
    camera_session_ = nullptr;
  }

  camera_device_ = nullptr;

  pthread_mutex_unlock(&lock_);

  return res;
}

int32_t Camera3DeviceClient::EndConfigure(const StreamConfiguration& stream_config) {

  if (NULL == camera_session_.get()) {
    return -ENODEV;
  }

  if (stream_config.is_constrained_high_speed && !is_hfr_supported_) {
    CAMERA_ERROR("%s: HFR mode is not supported by this camera!\n", __func__);
    return -EINVAL;
  }

  return ConfigureStreams(stream_config);

}

int32_t Camera3DeviceClient::ConfigureStreams(const StreamConfiguration& stream_config) {

  pthread_mutex_lock(&lock_);

  hfr_mode_enabled_ = stream_config.is_constrained_high_speed;
  is_raw_only_ = stream_config.is_raw_only;
  batch_size_ = stream_config.batch_size;
  frame_rate_range_[0] = stream_config.frame_rate_range[0];
  frame_rate_range_[1] = stream_config.frame_rate_range[1];

  if (stream_config.params) {
    cam_feature_flags_ |= stream_config.params->cam_feature_flags;
  }

#ifdef USE_FPS_IDX
  fps_sensormode_index_ = stream_config.fps_sensormode_index;
#endif
  bool res = ConfigureStreamsLocked();

  pthread_mutex_unlock(&lock_);

  return res;
}

int32_t Camera3DeviceClient::ConfigureStreamsLocked() {
  status_t res = 0;
  if (state_ != STATE_NOT_CONFIGURED && state_ != STATE_CONFIGURED) {
    CAMERA_ERROR("%s: Not idle\n", __func__);
    return -ENOSYS;
  }

  if (!reconfig_) {
    CAMERA_ERROR("%s: Skipping config, no stream changes\n", __func__);
    return 0;
  }

  ::aidl::android::hardware::camera::device::StreamConfiguration config{};

  config.operationMode = GetOpMode();

  CAMERA_INFO("%s: operation_mode: 0x%x \n", __func__, config.operationMode);

#if defined(CAMERA_HAL_API_VERSION) && (CAMERA_HAL_API_VERSION >= 0x0305)
  if (hfr_mode_enabled_) {
    camera_metadata_t *session_parameters = allocate_camera_metadata(1, 128);
    add_camera_metadata_entry(session_parameters,
                              ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                              frame_rate_range_, 2);

    config.session_parameters = session_parameters;
  }
#endif

  std::vector<Stream> streams;
  for (size_t i = 0; i < streams_.size(); i++) {
    Stream *outputStream;
    outputStream = streams_.editValueAt(i)->BeginConfigure();
    if (outputStream == NULL) {
      CAMERA_ERROR("%s: Can't start stream configuration\n", __func__);
      return -ENOSYS;
    }
    streams.push_back(*outputStream);
  }

#if 0 //TODO: Enable Input stream
  if (0 <= input_stream_.stream_id) {
    input_stream_.usage = 0; //Reset any previously set usage flags from Hal
    streams.push_back(&input_stream_);
  }
#endif
  config.streams = streams;
  std::vector<HalStream> halConfigs;
  ret_ = camera_session_->configureStreams(config, &halConfigs);

  if (!ret_.isOk()) {
    CAMERA_ERROR("Configure Stream returned error");
    for (uint32_t i = 0; i < streams_.size(); i++) {
      Camera3Stream *stream = streams_.editValueAt(i);
      if (stream->IsConfigureActive()) {
        CAMERA_ERROR("Configure Stream returned error_1");
        res = stream->AbortConfigure();
        if (0 != res) {
          CAMERA_ERROR("Can't abort stream %d configure: %s (%d)\n",
                     stream->GetId(), strerror(-res), res);
          return res;
        }
      }
    }

    InternalUpdateStatusLocked(STATE_NOT_CONFIGURED);
    reconfig_ = true;

    return -EINVAL;
  } else if (0 != res) {
    CAMERA_ERROR("%s: Unable to configure streams with HAL: %s (%d)\n", __func__,
               strerror(-res), res);
    return res;
  }

  for (uint32_t i = 0; i < streams_.size(); i++) {
    Camera3Stream *outputStream = streams_.editValueAt(i);
    if (outputStream->IsConfigureActive()) {
      res = outputStream->EndConfigure();
      if (0 != res) {
        CAMERA_ERROR(
            "%s: Unable to complete stream configuration"
            "%d: %s (%d)\n",
            __func__, outputStream->GetId(), strerror(-res), res);
        return res;
      }
    }
  }

  request_handler_.FinishConfiguration(batch_size_);
  reconfig_ = false;
  frame_number_ = 0;
  InternalUpdateStatusLocked(STATE_CONFIGURED);

  std::vector<Camera3Stream* >::iterator it = deleted_streams_.begin();
  while (it != deleted_streams_.end()) {
    Camera3Stream *stream = *it;
    it = deleted_streams_.erase(it);
    delete stream;

  }
  deleted_streams_.clear();

  return 0;
}

int32_t Camera3DeviceClient::DeleteStream(int streamId, bool cache) {
  int32_t res = 0;
  Camera3Stream *stream;
  int32_t outputStreamIdx;
  pthread_mutex_lock(&lock_);

  switch (state_) {
    case STATE_ERROR:
      CAMERA_ERROR("%s: Device has encountered a serious error\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_INITIALIZED:
    case STATE_CLOSED:
      CAMERA_ERROR("%s: Device not initialized\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_CONFIGURED:
    case STATE_CONFIGURED:
    case STATE_RUNNING:
      if (!cache) {
        CAMERA_INFO("%s: Stream is not cached, Issue internal reconfig!",
            __func__);
        res = InternalPauseAndWaitLocked();
        if (0 != res) {
          SET_ERR_L("Can't pause captures to reconfigure streams!");
          goto exit;
        }
      }
      break;
    default:
      CAMERA_ERROR("%s: Unknown state: %d\n", __func__, state_);
      res = -ENOSYS;
      goto exit;
  }

  if (streamId == input_stream_.stream_id) {
    input_stream_.stream_id = -1;
  } else {
    outputStreamIdx = streams_.indexOfKey(streamId);
    if (outputStreamIdx == -ENOENT) {
      CAMERA_ERROR("%s: Stream %d does not exist\n", __func__, streamId);
      res = -EINVAL;
      goto exit;
    }

    stream = streams_.editValueAt(outputStreamIdx);
    if (request_handler_.IsStreamActive(*stream)) {
      CAMERA_ERROR("%s: Stream %d still has pending requests\n", __func__,
                 streamId);
      res = -ENOSYS;
      goto exit;
    }

    streams_.removeItem(streamId);

    if (streams_.isEmpty()) {
      cam_feature_flags_ = static_cast<uint32_t>(CamFeatureFlag::kNone);
    }

    res = stream->Close();
    if (0 != res) {
      CAMERA_ERROR("%s: Can't close deleted stream %d\n", __func__, streamId);
    }
    if (!cache && !streams_.isEmpty()) {
      reconfig_ = true;
      res = ConfigureStreamsLocked();
      if (0 != res) {
        CAMERA_ERROR("%s: Can't reconfigure device for new stream %d: %s (%d)",
                 __func__, next_stream_id_, strerror(-res), res);
        goto exit;
      }
      InternalResumeLocked();
    } else if (!cache && streams_.isEmpty()) {
      InternalResumeLocked();
    } else {
      // In this scenario stream will be cached and will not trigger stream
      // reconfiguration. reconfiguration will be triggered in next round of
      // updating streaming capture request - creating a brand new stream or
      // deleting an existing stream without caching.
      if (state_ != STATE_RUNNING) {
        // Avoid reconfiguration if any existing stream is running, otherwise
        // updating capture request for setting parameters will try to
        // reconfigure it.
        reconfig_ = true;
      }
      deleted_streams_.push_back(stream);
    }
  }

exit:

  pthread_mutex_unlock(&lock_);

  return res;
}

int32_t Camera3DeviceClient::CreateInputStream(
    const CameraInputStreamParameters &inputConfiguration) {
  int32_t res = 0;
  bool wasActive = false;
  pthread_mutex_lock(&lock_);

  if ((nullptr == inputConfiguration.get_input_buffer) ||
      (nullptr == inputConfiguration.return_input_buffer)) {
    CAMERA_ERROR("%s: Input stream callbacks are invalid!\n", __func__);
    res = -EINVAL;
    goto exit;
  }

  if (0 <= input_stream_.stream_id) {
    CAMERA_ERROR("%s: Only one input stream can be created at any time!\n",
               __func__);
    res = -ENOSYS;
    goto exit;
  }

  switch (state_) {
    case STATE_ERROR:
      CAMERA_ERROR("%s: Device has encountered a serious error\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_INITIALIZED:
    case STATE_CLOSED:
      CAMERA_ERROR("%s: Device not initialized\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_CONFIGURED:
    case STATE_CONFIGURED:
      break;
    case STATE_RUNNING:
      res = InternalPauseAndWaitLocked();
      if (0 != res) {
        SET_ERR_L("Can't pause captures to reconfigure streams!");
        goto exit;
      }
      wasActive = true;
      break;
    default:
      CAMERA_ERROR("%s: Unknown state: %d\n", __func__, state_);
      res = -ENOSYS;
      goto exit;
  }
  assert(state_ != STATE_RUNNING);

  reconfig_ = true;

  input_stream_ = {};
  input_stream_.width = inputConfiguration.width;
  input_stream_.height = inputConfiguration.height;
  input_stream_.format = inputConfiguration.format;
  input_stream_.get_input_buffer = inputConfiguration.get_input_buffer;
  input_stream_.return_input_buffer = inputConfiguration.return_input_buffer;
  input_stream_.stream_id = next_stream_id_++;
  input_stream_.stream_type = StreamType::INPUT;

  // Continue captures if active at start
  if (wasActive) {
    res = ConfigureStreamsLocked();
    if (0 != res) {
      CAMERA_ERROR("%s: Can't reconfigure device for new stream %d: %s (%d)",
                 __func__, next_stream_id_, strerror(-res), res);
      goto exit;
    }
    InternalResumeLocked();
  }

  res = input_stream_.stream_id;

exit:

    pthread_mutex_unlock(&lock_);

    return res;
}

int32_t Camera3DeviceClient::CreateStream(
    const CameraStreamParameters &outputConfiguration) {
  CAMERA_DEBUG("%s: Camera Flags: %x\n", __func__,
      outputConfiguration.cam_feature_flags);
  int32_t res = 0;
  Camera3Stream *newStream = NULL;
  int32_t blobBufferSize = 0;
  bool wasActive = false;
  pthread_mutex_lock(&lock_);

  if (nullptr == outputConfiguration.cb) {
    CAMERA_ERROR("%s: Stream callback invalid!\n", __func__);
    res = -EINVAL;
    goto exit;
  }

  switch (state_) {
    case STATE_ERROR:
      CAMERA_ERROR("%s: Device has encountered a serious error\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_INITIALIZED:
    case STATE_CLOSED:
      CAMERA_ERROR("%s: Device not initialized\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_CONFIGURED:
    case STATE_CONFIGURED:
      break;
    case STATE_RUNNING:
      res = InternalPauseAndWaitLocked();
      if (0 != res) {
        SET_ERR_L("Can't pause captures to reconfigure streams!");
        goto exit;
      }
      wasActive = true;
      break;
    default:
      CAMERA_ERROR("%s: Unknown state: %d\n", __func__, state_);
      res = -ENOSYS;
      goto exit;
  }
  assert(state_ != STATE_RUNNING);

  if (outputConfiguration.format == PixelFormat::BLOB) {
    blobBufferSize = CaclulateBlobSize(outputConfiguration.width,
                                       outputConfiguration.height);
    if (blobBufferSize <= 0) {
      CAMERA_ERROR("%s: Invalid jpeg buffer size %zd\n", __func__,
                 blobBufferSize);
      res = -EINVAL;
      goto exit;
    }
  }

  newStream = new Camera3Stream(next_stream_id_, blobBufferSize,
                                outputConfiguration, alloc_device_interface_, monitor_);
  if (NULL == newStream) {
    res = -ENOMEM;
    goto exit;
  }

  res = streams_.add(next_stream_id_, newStream);
  if (res < 0) {
    CAMERA_ERROR("%s: Can't add new stream to set: %s (%d)\n", __func__,
               strerror(-res), res);
    goto exit;
  }

  reconfig_ = true;

  // Continue captures if active at start
  if (wasActive) {
    res = ConfigureStreamsLocked();
    if (0 != res) {
      CAMERA_ERROR("%s: Can't reconfigure device for new stream %d: %s (%d)",
                 __func__, next_stream_id_, strerror(-res), res);
      goto exit;
    }
    InternalResumeLocked();
  }

  res = next_stream_id_++;

exit:

  pthread_mutex_unlock(&lock_);
  return res;
}

int32_t Camera3DeviceClient::QueryMaxBlobSize(int32_t &maxBlobWidth,
                                              int32_t &maxBlobHeight) {
  maxBlobWidth = 0;
  maxBlobHeight = 0;
  camera_metadata_entry_t availableStreamConfigs =
      device_info_.find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
  if (availableStreamConfigs.count == 0 ||
      availableStreamConfigs.count % 4 != 0) {
    return 0;
  }

  for (uint32_t i = 0; i < availableStreamConfigs.count; i += 4) {
    int32_t format = availableStreamConfigs.data.i32[i];
    int32_t width = availableStreamConfigs.data.i32[i + 1];
    int32_t height = availableStreamConfigs.data.i32[i + 2];
    int32_t isInput = availableStreamConfigs.data.i32[i + 3];
    if (isInput == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
        format == HAL_PIXEL_FORMAT_BLOB &&
        (width * height > maxBlobWidth * maxBlobHeight)) {
      maxBlobWidth = width;
      maxBlobHeight = height;
    }
  }

  return 0;
}

int32_t Camera3DeviceClient::CaclulateBlobSize(int32_t width, int32_t height) {
  // Get max jpeg size (area-wise).
  int32_t maxJpegSizeWidth = 0;
  int32_t maxJpegSizeHeight = 0;
  QueryMaxBlobSize(maxJpegSizeWidth, maxJpegSizeHeight);
  if (maxJpegSizeWidth == 0) {
    CAMERA_ERROR(
        "%s: Camera %d: Can't find valid available jpeg sizes in "
        "static metadata!\n",
        __func__, id_);
    return -EINVAL;
  }

  // Get max jpeg buffer size
  int32_t maxJpegBufferSize = 0;
  camera_metadata_entry jpegBufMaxSize =
      device_info_.find(ANDROID_JPEG_MAX_SIZE);
  if (jpegBufMaxSize.count == 0) {
    CAMERA_ERROR(
        "%s: Camera %d: Can't find maximum JPEG size in static"
        " metadata!\n",
        __func__, id_);
    return -EINVAL;
  }
  maxJpegBufferSize = jpegBufMaxSize.data.i32[0];
  assert(JPEG_BUFFER_SIZE_MIN < maxJpegBufferSize);

  ssize_t jpegBufferSize = width * height;
  if (jpegBufferSize > maxJpegBufferSize) {
    jpegBufferSize = maxJpegBufferSize;
  }

  return jpegBufferSize;
}

int32_t Camera3DeviceClient::CreateDefaultRequest(int templateId,
                                                  CameraMetadata *request) {
  int32_t res = 0;
  const camera_metadata_t *rawRequest = nullptr;

  pthread_mutex_lock(&lock_);

  switch (state_) {
    case STATE_ERROR:
      CAMERA_ERROR("%s: Device has encountered a serious error\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_INITIALIZED:
    case STATE_CLOSED:
      CAMERA_ERROR("%s: Device is not initialized!\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_CONFIGURED:
    case STATE_CONFIGURED:
    case STATE_RUNNING:
      break;
    default:
      CAMERA_ERROR("%s: Unexpected status: %d", __func__, state_);
      res = -ENOSYS;
      goto exit;
  }

  if (!request_templates_[templateId].isEmpty()) {
    *request = request_templates_[templateId];
    goto exit;
  }

  {
    RequestTemplate t = (RequestTemplate) templateId;
    ::aidl::android::hardware::camera::device::CameraMetadata req;
    ret_ = camera_session_->constructDefaultRequestSettings(t, &req);
    rawRequest = reinterpret_cast<const camera_metadata_t*>(req.metadata.data());
  }

  if(!ret_.isOk()){
    CAMERA_ERROR("%s: constructDefaultRequestSettings returns status:%d:%d",__func__,ret_.getExceptionCode(),
                  ret_.getServiceSpecificError());
    res = -EINVAL;
    goto exit;
  }

  *request = rawRequest;
  request_templates_[templateId] = rawRequest;

exit:

  pthread_mutex_unlock(&lock_);
  return res;
}

int32_t Camera3DeviceClient::CreateDefaultRequest(RequestTemplate templateId,
                                                  CameraMetadata *request) {
  return CreateDefaultRequest((int) templateId, request);
}

int32_t Camera3DeviceClient::MarkPendingRequest(
    uint32_t frameNumber, int32_t numBuffers,
    CaptureResultExtras resultExtras) {
  pthread_mutex_lock(&pending_requests_lock_);

  pending_requests_vector_.emplace(frameNumber,
                                  PendingRequest(numBuffers, resultExtras));

  pthread_mutex_unlock(&pending_requests_lock_);

  return 0;
}

bool Camera3DeviceClient::HandlePartialResult(
    uint32_t frameNumber, const CameraMetadata &partial,
    const CaptureResultExtras &resultExtras) {

  if (nullptr != client_cb_.resultCb) {
    CaptureResult captureResult;
    captureResult.resultExtras = resultExtras;
    captureResult.metadata = partial;

    if (!UpdatePartialTag(captureResult.metadata, ANDROID_REQUEST_FRAME_COUNT,
                          reinterpret_cast<int32_t *>(&frameNumber),
                          frameNumber)) {
      return false;
    }

    int32_t requestId = resultExtras.requestId;
    if (!UpdatePartialTag(captureResult.metadata, ANDROID_REQUEST_ID,
                          &requestId, frameNumber)) {
      return false;
    }
#if 0 // TODO: get device API version
    if (device_->common.version < CAMERA_DEVICE_API_VERSION_3_2) {
      static const uint8_t partialResult =
          ANDROID_QUIRKS_PARTIAL_RESULT_PARTIAL;
      if (!UpdatePartialTag(captureResult.metadata,
                            ANDROID_QUIRKS_PARTIAL_RESULT, &partialResult,
                            frameNumber)) {
        return false;
      }
    }
#endif
    client_cb_.resultCb(captureResult);
  }

  return true;
}

template <typename T>
bool Camera3DeviceClient::QueryPartialTag(const CameraMetadata &result,
                                          int32_t tag, T *value,
                                          uint32_t frameNumber) {
  (void)frameNumber;

  camera_metadata_ro_entry_t entry;

  entry = result.find(tag);
  if (entry.count == 0) {
    return false;
  }

  if (sizeof(T) == sizeof(uint8_t)) {
    *value = entry.data.u8[0];
  } else if (sizeof(T) == sizeof(int32_t)) {
    *value = entry.data.i32[0];
  } else {
    return false;
  }
  return true;
}

template <typename T>
bool Camera3DeviceClient::UpdatePartialTag(CameraMetadata &result, int32_t tag,
                                           const T *value,
                                           uint32_t frameNumber) {
  if (0 != result.update(tag, value, 1)) {
    return false;
  }
  return true;
}

int32_t Camera3DeviceClient::CancelRequest(int requestId,
                                           int64_t *lastFrameNumber) {
  std::vector<int32_t>::iterator it, end;
  int32_t res = 0;

  pthread_mutex_lock(&lock_);

  switch (state_) {
    case STATE_ERROR:
      CAMERA_ERROR("%s: Device has encountered a serious error\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_INITIALIZED:
    case STATE_CLOSED:
      CAMERA_ERROR("%s: Device not initialized\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_CONFIGURED:
    case STATE_CONFIGURED:
    case STATE_RUNNING:
      break;
    default:
      SET_ERR_L("Unknown state: %d", state_);
      res = -ENOSYS;
      goto exit;
  }

  for (it = repeating_requests_.begin(), end = repeating_requests_.end();
       it != end; ++it) {
    if (*it == requestId) {
      break;
    }
  }

  if (it == end) {
    CAMERA_ERROR(
        "%s: Camera%d: Did not find request id %d in list of"
        " streaming requests",
        __FUNCTION__, id_, requestId);
    res = -EINVAL;
    goto exit;
  }

  res = request_handler_.ClearRepeatingRequests(lastFrameNumber);
  if (0 == res) {
    repeating_requests_.clear();
  }

exit:

  pthread_mutex_unlock(&lock_);
  return res;
}
void Camera3DeviceClient::HandleCaptureResult(
    const ::aidl::android::hardware::camera::device::CaptureResult &result) {
  int32_t res;
  ::aidl::android::hardware::camera::device::CameraMetadata resultMetadata;
  uint32_t frameNumber = result.frameNumber;
  bool isPartialResult = false;

  if (result.result.metadata.size() == 0 && result.outputBuffers.size() == 0) {
    //SET_ERR("No result data provided by HAL for frame %d", frameNumber);
    return;
  }

  if (!is_partial_result_supported_ && sizeof(result.result) > 0 &&
      result.partialResult != 1) {
    SET_ERR(
        "Result is invalid for frame %d: partial_result %u should be 1"
        " when partial result are not supported\n",
        frameNumber, result.partialResult);
    return;
  }
    if (result.fmqResultSize > 0) {
        resultMetadata.metadata.resize(result.fmqResultSize);
        if (nullptr == result_metadata_queue_) {
            SET_ERR("%s: mResultMetadataQueue is nullptr", __func__);
            return;
        }
        if (!result_metadata_queue_->read(reinterpret_cast<int8_t*>(resultMetadata.metadata.data()), result.fmqResultSize)) {
            SET_ERR("%s: Read operation failed", __func__);
            return;
        }
    }
  CameraMetadata collectedPartialResult;
  camera_metadata_ro_entry_t entry;
  uint32_t numBuffersReturned;

  int64_t shutterTimestamp = 0;

  pthread_mutex_lock(&pending_requests_lock_);
  if (!pending_requests_vector_.count(frameNumber)) {
    if (pending_error_requests_vector_.count(frameNumber)) {
      CAMERA_INFO("%s: Found a request with error status. "
          "Ignore the capture result.\n", __func__);
      pending_error_requests_vector_.erase(frameNumber);
      pthread_mutex_unlock(&pending_requests_lock_);
      return;
    }
    SET_ERR("Invalid frame number in capture result: %d", frameNumber);
    pthread_mutex_unlock(&pending_requests_lock_);
    return;
  }
  PendingRequest &request = pending_requests_vector_.at(frameNumber);
  CAMERA_DEBUG(
      "%s: Received PendingRequest requestId = %d, frameNumber = %d,"
      "burstId = %d, partialResultCount = %d\n request buffersRemaining = %d",
      __func__, request.resultExtras.requestId,
      request.resultExtras.frameNumber, request.resultExtras.burstId,
      result.partialResult, request.buffersRemaining);
  if (result.partialResult != 0)
    request.resultExtras.partialResultCount = result.partialResult;
  if (is_partial_result_supported_ && result.result.metadata.size() > 0) {
    if (result.partialResult > partial_result_count_ ||
        result.partialResult < 1) {
      SET_ERR(
          "Result is invalid for frame %d: partial_result %u"
          "should be in the range of [1, %d] when meta gets included"
          "in the result",
          frameNumber, result.partialResult, partial_result_count_);
      goto exit;
    }
    isPartialResult = (result.partialResult < partial_result_count_);
    if (isPartialResult) {
      request.partialResult.composedResult.clear();
      request.partialResult.composedResult.append(
          (const camera_metadata_t *)result.result.metadata.data());
    }
    if (isPartialResult) {
      request.partialResult.partial3AReceived = HandlePartialResult(
          frameNumber, request.partialResult.composedResult,
          request.resultExtras);
    }
  }

  shutterTimestamp = request.shutterTS;

  if (result.result.metadata.size() > 0 && !isPartialResult) {
    if (request.isMetaPresent) {
      SET_ERR("Called several times with meta for frame %d", frameNumber);
      goto exit;
    }
    if (is_partial_result_supported_ &&
        !request.partialResult.composedResult.isEmpty()) {
      collectedPartialResult.acquire(request.partialResult.composedResult);
    }
    request.isMetaPresent = true;
  }

  numBuffersReturned = result.outputBuffers.size();
  request.buffersRemaining -= numBuffersReturned;
  if (!result.inputBuffer.buffer.fds.empty()) {
    request.buffersRemaining--;
  }
  if (request.buffersRemaining < 0) {
    SET_ERR("Too many buffers returned for frame %d", frameNumber);
    goto exit;
  }

  res = find_camera_metadata_ro_entry(
      (const camera_metadata_t *) result.result.metadata.data(),
      ANDROID_SENSOR_TIMESTAMP, &entry);

  if ((0 == res) && (entry.count == 1)) {
    request.sensorTS = entry.data.i64[0];
  }

  request.pendingBuffers.resize(result.outputBuffers.size());

 {
   if ((shutterTimestamp == 0) && (result.outputBuffers.size() > 0)) {
     std::vector<::aidl::android::hardware::camera::device::StreamBuffer>& outputBuffers =
     const_cast<std::vector<::aidl::android::hardware::camera::device::StreamBuffer>&>(result.outputBuffers);
     request.pendingBuffers = std::move(outputBuffers);
   }
 }

  if (result.result.metadata.size() > 0 && !isPartialResult) {
    if (shutterTimestamp == 0) {
      request.pendingMetadata.clear();
      request.pendingMetadata.append(
          (const camera_metadata_t *)resultMetadata.metadata.data());
      request.partialResult.composedResult = collectedPartialResult;
    } else {
      CameraMetadata metadata((camera_metadata_t *) resultMetadata.metadata.data());
      SendCaptureResult(metadata, request.resultExtras, collectedPartialResult,
                        frameNumber);
    }
  }

  if (0 < shutterTimestamp) {
    ReturnOutputBuffers(result.outputBuffers.data(), result.outputBuffers.size(),
                       shutterTimestamp, result.frameNumber);
  }

  RemovePendingRequestLocked(frameNumber);
  pthread_mutex_unlock(&pending_requests_lock_);

#if 0 //TODO: Handle input buffer streams properly
  if (NULL != result.inputBuffer.buffer) {
    StreamBuffer input_buffer;
    memset(&input_buffer, 0, sizeof(input_buffer));

    uint32_t stream_id = result.inputBuffer.streamId;
    Camera3Stream *stream = streams_.valueFor(stream_id);

    input_buffer.stream_id = stream_id;
    //TODO: Find a way to extract data_space flags from stream buffer.
    //input_buffer.data_space = input_stream->data_space;
    input_buffer.handle =
      input_stream->buffers_map[result.inputBuffer.buffer.getNativeHandle()];
    input_stream->buffers_map.erase(result.inputBuffer.buffer.getNativeHandle());
    input_stream->return_input_buffer(input_buffer);
    input_stream->input_buffer_cnt--;
  }
#endif
  return;

exit:
  pthread_mutex_unlock(&pending_requests_lock_);
}

ScopedAStatus Camera3DeviceClient::NotifyError(const ErrorMsg &msg) {
  std::map<ErrorCode,CameraErrorCode> error_map;
  error_map[static_cast<ErrorCode>(0)] = ERROR_CAMERA_INVALID_ERROR;
  error_map[ErrorCode::ERROR_DEVICE] = ERROR_CAMERA_DEVICE;
  error_map[ErrorCode::ERROR_REQUEST] = ERROR_CAMERA_REQUEST;
  error_map[ErrorCode::ERROR_RESULT] = ERROR_CAMERA_RESULT;
  error_map[ErrorCode::ERROR_BUFFER] = ERROR_CAMERA_BUFFER;

  CameraErrorCode errorCode =
      (error_map.find(msg.errorCode) != error_map.end())
          ? error_map[msg.errorCode]
          : ERROR_CAMERA_INVALID_ERROR;

  CaptureResultExtras resultExtras;
  switch (errorCode) {
    case ERROR_CAMERA_DEVICE:
      SET_ERR("Camera HAL reported serious device error");
      break;
    case ERROR_CAMERA_REQUEST:
    case ERROR_CAMERA_RESULT:
    case ERROR_CAMERA_BUFFER:
      pthread_mutex_lock(&pending_requests_lock_);
      if (pending_requests_vector_.count(msg.frameNumber)) {
        PendingRequest &r = pending_requests_vector_.at(msg.frameNumber);
        r.status = static_cast<int>(msg.errorCode);
        resultExtras = r.resultExtras;
      } else {
        resultExtras.frameNumber = msg.frameNumber;
        CAMERA_ERROR(
            "%s: Camera %d: cannot find pending request for "
            "frame %u error\n",
            __func__, id_, resultExtras.frameNumber);
      }
      pthread_mutex_unlock(&pending_requests_lock_);
      if (flush_on_going_ == false) {
        if (nullptr != client_cb_.errorCb) {
          client_cb_.errorCb(errorCode, resultExtras);
        } else {
          CAMERA_ERROR("%s: Camera %d: no listener available\n", __func__, id_);
        }
      }
      break;
    default:
      SET_ERR("Unknown error message from HAL: %d", msg.errorCode);
      break;
  }
  return ScopedAStatus::ok();
}

ScopedAStatus Camera3DeviceClient::NotifyShutter(const ShutterMsg &msg) {
  pthread_mutex_lock(&pending_requests_lock_);
  bool pending_request_found = false;
  if (pending_requests_vector_.count(msg.frameNumber)) {
    pending_request_found = true;
    PendingRequest &r = pending_requests_vector_.at(msg.frameNumber);

    if (nullptr != client_cb_.shutterCb) {
      client_cb_.shutterCb(r.resultExtras, msg.timestamp);
    }

    if (r.resultExtras.input) {
      if (msg.frameNumber < next_shutter_input_frame_number_) {
        SET_ERR(
            "Shutter notification out-of-order. Expected "
            "notification for frame %d, got frame %d",
            next_shutter_input_frame_number_, msg.frameNumber);
        pthread_mutex_unlock(&pending_requests_lock_);
        return ScopedAStatus::ok();
      }
      next_shutter_input_frame_number_ = msg.frameNumber + 1;
    } else {
      if (msg.frameNumber < next_shutter_frame_number_) {
        SET_ERR(
            "Shutter notification out-of-order. Expected "
            "notification for frame %d, got frame %d",
            next_shutter_frame_number_, msg.frameNumber);
        pthread_mutex_unlock(&pending_requests_lock_);
        return ScopedAStatus::ok();
      }
      next_shutter_frame_number_ = msg.frameNumber + 1;
    }

    r.shutterTS = msg.timestamp;

    SendCaptureResult(r.pendingMetadata, r.resultExtras,
                      r.partialResult.composedResult, msg.frameNumber);
    ReturnOutputBuffers(r.pendingBuffers.data(), r.pendingBuffers.size(),
                        r.shutterTS, msg.frameNumber);
    r.pendingBuffers.clear();

    RemovePendingRequestLocked(msg.frameNumber);
  }
  pthread_mutex_unlock(&pending_requests_lock_);

  if (!pending_request_found) {
    SET_ERR("Shutter notification with invalid frame number %d",
            msg.frameNumber);
  }
  return ScopedAStatus::ok();
}

void Camera3DeviceClient::SendCaptureResult(
    CameraMetadata &pendingMetadata, CaptureResultExtras &resultExtras,
    CameraMetadata &collectedPartialResult, uint32_t frameNumber) {
  if (pendingMetadata.isEmpty()) return;

  if (nullptr == client_cb_.resultCb) {
    return;
  }

  if (resultExtras.input) {
    if (frameNumber < next_result_input_frame_number_) {
      SET_ERR(
          "Out-of-order result received! "
          "(arriving frame number %d, expecting %d)",
          frameNumber, next_result_input_frame_number_);
      return;
    }
    next_result_input_frame_number_ = frameNumber + 1;
  } else {
    if (frameNumber < next_result_frame_number_) {
      SET_ERR(
          "Out-of-order result received! "
          "(arriving frame number %d, expecting %d)",
          frameNumber, next_result_frame_number_);
      return;
    }
    next_result_frame_number_ = frameNumber + 1;
  }

  CaptureResult captureResult;
  captureResult.resultExtras = resultExtras;
  captureResult.metadata = pendingMetadata;

  if (captureResult.metadata.update(ANDROID_REQUEST_FRAME_COUNT,
                                    (int32_t *)&frameNumber, 1) != 0) {
    SET_ERR("Unable to update frame number (%d)", frameNumber);
    return;
  }

  if (is_partial_result_supported_ && !collectedPartialResult.isEmpty()) {
    captureResult.metadata.append(collectedPartialResult);
  }

  captureResult.metadata.sort();

  camera_metadata_entry entry =
      captureResult.metadata.find(ANDROID_SENSOR_TIMESTAMP);
  if (entry.count == 0) {
    SET_ERR("No timestamp from Hal for frame %d!", frameNumber);
    return;
  }

  client_cb_.resultCb(captureResult);
}

void Camera3DeviceClient::ReturnOutputBuffers(
    const ::aidl::android::hardware::camera::device::StreamBuffer *outputBuffers, size_t numBuffers,
    int64_t timestamp, int64_t frame_number) {
  for (size_t i = 0; i < numBuffers; i++) {
    Camera3Stream *stream = streams_.valueFor(outputBuffers[i].streamId);
    stream->ReturnBufferToClient(outputBuffers[i], timestamp, frame_number);

    if (BufferStatus::ERROR == outputBuffers[i].status &&
        flush_on_going_ == false) {
      CaptureResultExtras resultExtras;
      if (pending_requests_vector_.count(frame_number)) {
        PendingRequest &r = pending_requests_vector_.at(frame_number);
        r.status = (int) ErrorCode::ERROR_BUFFER;
        resultExtras = r.resultExtras;
      } else {
        resultExtras.frameNumber = frame_number;
        CAMERA_ERROR("%s: Camera %d: cannot find pending request for "
            "frame %u\n", __func__, id_, resultExtras.frameNumber);
      }
      client_cb_.errorCb(ERROR_CAMERA_BUFFER, resultExtras);
    }
  }
}

ScopedAStatus Camera3DeviceClient::requestStreamBuffers(
    const std::vector<aidl::android::hardware::camera::device::BufferRequest>& bufReqs,
    std::vector<aidl::android::hardware::camera::device::StreamBufferRet>* outBuffers,
    aidl::android::hardware::camera::device::BufferRequestStatus* status){
  return ScopedAStatus::ok();
}

ScopedAStatus Camera3DeviceClient::returnStreamBuffers(const std::vector<::aidl::android::hardware::camera::device::StreamBuffer>& in_buffers) {
    return ndk::ScopedAStatus::ok();
}

ScopedAStatus Camera3DeviceClient::ReturnStreamBuffer(StreamBuffer buffer) {
  Camera3Stream *stream;
  int32_t streamIdx;
  int32_t res = 0;
  pthread_mutex_lock(&lock_);

  switch (state_) {
    case STATE_ERROR:
      CAMERA_ERROR("%s: Device has encountered a serious error\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_INITIALIZED:
    case STATE_CLOSED:
    case STATE_NOT_CONFIGURED:
      CAMERA_ERROR("%s: Device is not initialized/configured!\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_CONFIGURED:
    case STATE_RUNNING:
      break;
    default:
      CAMERA_ERROR("%s: Unknown state: %d", __func__, state_);
      res = -ENOSYS;
      goto exit;
  }

  streamIdx = streams_.indexOfKey(buffer.stream_id);
  if (streamIdx == -ENOENT) {
    CAMERA_ERROR("%s: Stream %d does not exist\n", __func__, buffer.stream_id);
    res = -EINVAL;
    goto exit;
  }
  stream = streams_.editValueAt(streamIdx);
  if (0 != res) {
    CAMERA_ERROR("%s: Can't return buffer to its stream: %s (%d)\n", __func__,
               strerror(-res), res);
  }

  res = stream->ReturnBuffer(buffer);
exit:

  pthread_mutex_unlock(&lock_);
  return ScopedAStatus::ok();
}

void Camera3DeviceClient::RemovePendingRequestLocked(uint32_t frameNumber) {
  PendingRequest &request = pending_requests_vector_.at(frameNumber);

  int64_t sensorTS = request.sensorTS;
  int64_t shutterTS = request.shutterTS;

  if (request.buffersRemaining == 0 &&
      (0 != request.status || (request.isMetaPresent && shutterTS != 0))) {

    if (0 == request.status && sensorTS != shutterTS) {
      SET_ERR(
          "sensor timestamp (%ld) for frame %d doesn't match shutter"
          " timestamp (%ld)\n",
          sensorTS, frameNumber, shutterTS);
    }

    ReturnOutputBuffers(request.pendingBuffers.data(),
                        request.pendingBuffers.size(), 0,
                        frameNumber);

    if (0 != request.status && (!request.isMetaPresent || shutterTS == 0)) {
      CAMERA_INFO("%s: Received error in the capture request. Added to the error"
          " requests vector.\n", __func__);
      pending_error_requests_vector_.emplace(frameNumber, std::move(request));
    }

    pending_requests_vector_.erase(frameNumber);
  }
}

int32_t Camera3DeviceClient::GetCameraInfo(uint32_t idx, CameraMetadata *info) {
  if (NULL == info) {
    return -EINVAL;
  }

  if (idx >= number_of_cameras_) {
    return -EINVAL;
  }

  if (NULL == camera_device_.get()) {
    return -ENODEV;
  }

  {
    ::aidl::android::hardware::camera::device::CameraMetadata cameraCharacteristics;
    ret_ = camera_device_->getCameraCharacteristics(&cameraCharacteristics);
    if (!ret_.isOk()) {
      CAMERA_ERROR("%s: Error during camera static info query! \n", __func__);
      return -ENODEV;
    }
    auto camera_metadata = reinterpret_cast<camera_metadata_t*>(cameraCharacteristics.metadata.data());
    info->clear();
    info->append(camera_metadata);
  }
  return 0;
}

int32_t Camera3DeviceClient::SubmitRequest(Camera3Request request,
                                           bool streaming,
                                           int64_t *lastFrameNumber) {
  std::list<Camera3Request> requestList;
  requestList.push_back(request);
  return SubmitRequestList(requestList, streaming, lastFrameNumber);
}

int32_t Camera3DeviceClient::SubmitRequestList(std::list<Camera3Request> requests,
                                               bool streaming,
                                               int64_t *lastFrameNumber) {
  int32_t res = 0;
  if (requests.empty()) {
    CAMERA_ERROR("%s: Camera %d: Received empty!\n", __func__, id_);
    return -EINVAL;
  }

  List<const CameraMetadata> metadataRequestList;
  int32_t requestId = next_request_id_;
  int32_t temp_request_id = requestId;

  pthread_mutex_lock(&lock_);
  current_request_ids_.clear();

  switch (state_) {
    case STATE_ERROR:
      CAMERA_ERROR("%s: Device has encountered a serious error\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_INITIALIZED:
    case STATE_CLOSED:
      CAMERA_ERROR("%s: Device not initialized\n", __func__);
      res = -ENOSYS;
      goto exit;
    case STATE_NOT_CONFIGURED:
    case STATE_CONFIGURED:
    case STATE_RUNNING:
      break;
    default:
      CAMERA_ERROR("%s: Unknown state: %d", __func__, state_);
      res = -ENOSYS;
      goto exit;
  }

  for (std::list<Camera3Request>::iterator it = requests.begin();
       it != requests.end(); ++it) {
    Camera3Request request = *it;
    CameraMetadata metadata(request.metadata);
    if (metadata.isEmpty()) {
      CAMERA_ERROR("%s: Camera %d: Received invalid meta.\n", __func__, id_);
      res = -EINVAL;
      goto exit;
    } else if (request.streamIds.empty()) {
      CAMERA_ERROR(
          "%s: Camera %d: Requests must have at least one"
          " stream.\n",
          __func__, id_);
      res = -EINVAL;
      goto exit;
    }

    std::vector<int32_t> request_stream_id;
    request_stream_id.insert(request_stream_id.end(), request.streamIds.begin(),
                                 request.streamIds.end());
    std::sort(request_stream_id.begin(), request_stream_id.end());
    int32_t prev_id = -1;
    int32_t input_stream_idx = -1;
    for (uint32_t i = 0; i < request_stream_id.size(); ++i) {
      if (input_stream_.stream_id == request_stream_id[i]) {
        metadata.update(ANDROID_REQUEST_INPUT_STREAMS,
                        &input_stream_.stream_id, 1);
        input_stream_idx = i;
        continue;
      }
      Camera3Stream *stream = streams_.valueFor(request_stream_id[i]);

      if (NULL == stream) {
        CAMERA_ERROR("%s: Camera %d: Request contains invalid stream!\n",
                   __func__, id_);
        res = -EINVAL;
        goto exit;
      }

      if (prev_id == request_stream_id[i]) {
        CAMERA_ERROR("%s: Camera %d: Stream with id: %d appears several times in "
            "request!\n", __func__, id_, prev_id);
        res = -EINVAL;
        goto exit;
      } else {
        prev_id = request_stream_id[i];
      }
    }

    if (input_stream_idx >= 0 && input_stream_idx < request_stream_id.size()) {
      request_stream_id.erase(request_stream_id.begin() + input_stream_idx);
    }
    metadata.update(ANDROID_REQUEST_OUTPUT_STREAMS, &request_stream_id[0],
                    request_stream_id.size());

    metadata.update(ANDROID_REQUEST_ID, &temp_request_id, 1);
    metadataRequestList.push_back(metadata);
    current_request_ids_.push_back(temp_request_id);
    temp_request_id++;
  }
  next_request_id_ = temp_request_id;

  res = AddRequestListLocked(metadataRequestList, streaming, lastFrameNumber);
  if (0 != res) {
    CAMERA_ERROR("%s: Camera %d: Got error %d after trying to set capture\n",
               __func__, id_, res);
  }

  if (0 == res) {
    res = requestId;

    if (streaming) {
      repeating_requests_.push_back(requestId);
    }
  }

exit:
  pthread_mutex_unlock(&lock_);

  return res;
}

int32_t Camera3DeviceClient::AddRequestListLocked(
    const List<const CameraMetadata> &requests, bool streaming,
    int64_t *lastFrameNumber) {
  RequestList requestList;
  RequestList requestListReproc;

  int32_t res = GetRequestListLocked(requests, &requestList, &requestListReproc);
  if (0 != res) {
    return res;
  }

  if (requestList.empty() == requestListReproc.empty()) {
    CAMERA_ERROR("%s: Invalid request list. requests: %d reproc: %d\n", __func__,
      requestList.empty(), requestListReproc.empty());
    return -EINVAL;
  }

  if (!requestListReproc.empty()) {
    res = request_handler_.QueueReprocRequestList(requestListReproc,
        lastFrameNumber);
  } else if (!requestList.empty()) {
    if (streaming) {
      res = request_handler_.SetRepeatingRequests(requestList, lastFrameNumber);
    } else {
      res = request_handler_.QueueRequestList(requestList, lastFrameNumber);
    }
  }
  if (0 != res) {
    CAMERA_ERROR("%s: Request queue failed: %d reproc: %d\n", __func__, res,
        !requestListReproc.empty());
    return res;
  }

  WaitUntilStateThenRelock(true, WAIT_FOR_RUNNING);
  if (0 != res) {
    SET_ERR_L("Unable to change to running in %f seconds!",
              WAIT_FOR_RUNNING / 1e9);
  }

  return res;
}

int32_t Camera3DeviceClient::GetRequestListLocked(
    const List<const CameraMetadata> &metadataList,
    RequestList *requestList,
    RequestList *requestListReproc) {
  if (requestList == NULL) {
    CAMERA_ERROR("%s: Invalid requestList\n", __func__);
    return -EINVAL;
  }

  int32_t burstId = 0;
  for (List<const CameraMetadata>::const_iterator it = metadataList.begin();
       it != metadataList.end(); ++it) {
    CaptureRequest newRequest;
    int32_t res = GenerateCaptureRequestLocked(*it, newRequest);
    if (0 != res) {
      CAMERA_ERROR("%s: Can't create capture request\n", __func__);
      return -EINVAL;
    }

    // Setup burst Id and request Id
    newRequest.resultExtras.burstId = burstId++;
    if (it->exists(ANDROID_REQUEST_ID)) {
      if (it->find(ANDROID_REQUEST_ID).count == 0) {
        CAMERA_ERROR("%s: Empty RequestID\n", __func__);
        return -EINVAL;
      }
      newRequest.resultExtras.requestId =
          it->find(ANDROID_REQUEST_ID).data.i32[0];
    } else {
      CAMERA_ERROR("%s: RequestID missing\n", __func__);
      return -EINVAL;
    }

    if (newRequest.input == nullptr) {
      requestList->push_back(newRequest);
    } else {
      requestListReproc->push_back(newRequest);
    }
  }

  return 0;
}

int32_t Camera3DeviceClient::GenerateCaptureRequestLocked(
    const CameraMetadata &request, CaptureRequest &captureRequest) {
  int32_t res;

  if (state_ == STATE_NOT_CONFIGURED || reconfig_) {
    res = ConfigureStreamsLocked();
    if (res == BAD_VALUE && state_ == STATE_NOT_CONFIGURED) {
      CAMERA_ERROR("%s: No streams configured\n", __func__);
      return -EINVAL;
    }
    if (0 != res) {
      CAMERA_ERROR("%s: Can't set up streams: %s (%d)\n", __func__,
                 strerror(-res), res);
      return res;
    }
    if (state_ == STATE_NOT_CONFIGURED) {
      CAMERA_ERROR("%s: No streams configured\n", __func__);
      return -ENODEV;
    }
  }

  captureRequest.metadata = request;

  camera_metadata_entry_t streams =
      captureRequest.metadata.find(ANDROID_REQUEST_OUTPUT_STREAMS);
  if (streams.count == 0) {
    CAMERA_ERROR("%s: Zero output streams specified!\n", __func__);
    return -EINVAL;
  }

  for (uint32_t i = 0; i < streams.count; i++) {
    int idx = streams_.indexOfKey(streams.data.i32[i]);
    if (-ENOENT == idx) {
      CAMERA_ERROR("%s: Request references unknown stream %d\n", __func__,
                 streams.data.u8[i]);
      return -EINVAL;
    }
    Camera3Stream *stream = streams_.editValueAt(idx);

    if (stream->IsConfigureActive()) {
      res = stream->EndConfigure();
      if (0 != res) {
        CAMERA_ERROR("%s: Stream configuration failed %d: %s (%d)\n", __func__,
                   stream->GetId(), strerror(-res), res);
        return -ENODEV;
      }
    }

    if (stream->IsPrepareActive()) {
      CAMERA_ERROR("%s: Request contains a stream that is currently being"
          "prepared!\n", __func__);
        return -ENOSYS;
    }

    captureRequest.streams.push_back(stream);
  }
  captureRequest.metadata.erase(ANDROID_REQUEST_OUTPUT_STREAMS);

  captureRequest.input = NULL;
  captureRequest.resultExtras.input = false;
  if (captureRequest.metadata.exists(ANDROID_REQUEST_INPUT_STREAMS)) {
    streams =
          captureRequest.metadata.find(ANDROID_REQUEST_INPUT_STREAMS);
    if (1 == streams.count) {
      if (input_stream_.stream_id == streams.data.i32[0]) {
        captureRequest.input = &input_stream_;
        captureRequest.resultExtras.input = true;
      } else {
        CAMERA_ERROR("%s: Request contains input stream with id: %d that"
            "doesn't match the registered one: %d\n", __func__,
            streams.data.i32[0], input_stream_.stream_id);
          return -ENOSYS;
      }
    } else {
      CAMERA_ERROR("%s: Request contains multiple input streams: %d\n", __func__,
                 streams.count);
        return -ENOSYS;
    }
    captureRequest.metadata.erase(ANDROID_REQUEST_INPUT_STREAMS);
  }

  return 0;
}

void Camera3DeviceClient::SetErrorState(const char *fmt, ...) {
  pthread_mutex_lock(&lock_);
  va_list args;
  va_start(args, fmt);

  SetErrorStateLockedV(fmt, args);

  va_end(args);
  pthread_mutex_unlock(&lock_);
}

void Camera3DeviceClient::SetErrorStateV(const char *fmt, va_list args) {
  pthread_mutex_lock(&lock_);
  SetErrorStateLockedV(fmt, args);
  pthread_mutex_unlock(&lock_);
}

void Camera3DeviceClient::SetErrorStateLocked(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  SetErrorStateLockedV(fmt, args);

  va_end(args);
}

void Camera3DeviceClient::SetErrorStateLockedV(const char *fmt, va_list args) {
  String8 errorCause = String8::formatV(fmt, args);
  CAMERA_ERROR("%s: Camera %d: %s\n", __func__, id_, errorCause.string());

  if (state_ == STATE_ERROR || state_ == STATE_NOT_INITIALIZED ||
      state_ == STATE_CLOSED)
    return;

  last_error_ = errorCause;

  request_handler_.TogglePause(true);
  InternalUpdateStatusLocked(STATE_ERROR);

  if (nullptr != client_cb_.errorCb) {
    client_cb_.errorCb(ERROR_CAMERA_DEVICE, CaptureResultExtras());
  }
}

void Camera3DeviceClient::NotifyStatus(bool idle) {
  pthread_mutex_lock(&lock_);
  if (state_ != STATE_RUNNING && state_ != STATE_CONFIGURED) {
    pthread_mutex_unlock(&lock_);
    return;
  }
  InternalUpdateStatusLocked(idle ? STATE_CONFIGURED : STATE_RUNNING);

  if (pause_state_notify_) {
    pthread_mutex_unlock(&lock_);
    return;
  }

  pthread_mutex_unlock(&lock_);

  if (idle && nullptr != client_cb_.idleCb) {
    client_cb_.idleCb();
  }
}

int32_t Camera3DeviceClient::Flush(int64_t *lastFrameNumber) {
  int32_t res;
  pthread_mutex_lock(&lock_);
  flush_on_going_ = true;
  pthread_mutex_unlock(&lock_);

  // We can't hold locks during RequestHandler call to Clear() or HAL call to
  // flush. Some implementations will return buffers to client from the same
  // context and this can cause deadlock if client tries to return them.
  res = request_handler_.Clear(lastFrameNumber);
  if (0 != res) {
    CAMERA_ERROR("%s: Couldn't reset request handler, err: %d!", __func__, res);
    pthread_mutex_lock(&lock_);
    goto exit;
  }

  //TODO: ICameraDevice and ICameraSession do not have flush API.
  // Find proper way to flush the buffers.

  pthread_mutex_lock(&lock_);

  if (0 == res) {
    repeating_requests_.clear();
  }

exit:

  flush_on_going_ = false;
  pthread_mutex_unlock(&lock_);

  return res;
}

int32_t Camera3DeviceClient::WaitUntilIdle() {
  pthread_mutex_lock(&lock_);
  int32_t res = WaitUntilDrainedLocked();
  pthread_mutex_unlock(&lock_);

  return res;
}

int32_t Camera3DeviceClient::WaitUntilDrainedLocked() {
  switch (state_) {
    case STATE_NOT_INITIALIZED:
    case STATE_CLOSED:
    case STATE_NOT_CONFIGURED:
      return 0;
    case STATE_CONFIGURED:
    // To avoid race conditions, check with tracker to be sure
    case STATE_ERROR:
    case STATE_RUNNING:
      // Need to verify shut down
      break;
    default:
      SET_ERR_L("Unexpected status: %d", state_);
      return -ENOSYS;
  }

  int32_t res = WaitUntilStateThenRelock(false, WAIT_FOR_SHUTDOWN);
  if (0 != res) {
    SET_ERR_L("Error waiting for HAL to drain: %s (%d)", strerror(-res), res);
    for (uint32_t i = 0; i < streams_.size(); i++) {
      streams_[i]->PrintBuffersInfo();
    }
    if (input_stream_.stream_id != -1) {
      CAMERA_ERROR("%s: Input Stream: dim: %ux%u, fmt: %d "
          "input_buffer_cnt(%u)", __func__,
          input_stream_.width, input_stream_.height,
          input_stream_.format, input_stream_.input_buffer_cnt);
    }
  }

  pthread_mutex_lock(&pending_requests_lock_);
  pending_error_requests_vector_.clear();
  pthread_mutex_unlock(&pending_requests_lock_);

  return res;
}

void Camera3DeviceClient::InternalUpdateStatusLocked(State state) {
  state_ = state;
  current_state_updates_.push_back(state_);
  pthread_cond_broadcast(&state_updated_);
}

int32_t Camera3DeviceClient::InternalPauseAndWaitLocked() {
  request_handler_.TogglePause(true);
  pause_state_notify_ = true;

  int32_t res = WaitUntilStateThenRelock(false, WAIT_FOR_SHUTDOWN);
  if (0 != res) {
    SET_ERR_L("Can't idle device in %f seconds!", WAIT_FOR_SHUTDOWN / 1e9);
  }

  return res;
}

int32_t Camera3DeviceClient::InternalResumeLocked() {
  int32_t res = 0;

  bool pending_request;
  request_handler_.TogglePause(false, pending_request);
  if (pending_request == false) {
    return res;
  }

  res = WaitUntilStateThenRelock(true, WAIT_FOR_RUNNING);
  if (0 != res) {
    SET_ERR_L("Can't transition to active in %f seconds!",
              WAIT_FOR_RUNNING / 1e9);
  }

  pause_state_notify_ = false;
  return res;
}

int32_t Camera3DeviceClient::WaitUntilStateThenRelock(bool active,
                                                      int64_t timeout) {
  int32_t res = 0;

  uint32_t startIndex = 0;
  if (state_listeners_ == 0) {
    current_state_updates_.clear();
  } else {
    startIndex = current_state_updates_.size();
  }

  state_listeners_++;

  bool stateSeen = false;
  do {
    if (active == (state_ == STATE_RUNNING)) {
      break;
    }

    res = cond_wait_relative(&state_updated_, &lock_, timeout);
    if (0 != res) {
      break;
    }

    for (uint32_t i = startIndex; i < current_state_updates_.size(); i++) {
      if (active == (current_state_updates_[i] == STATE_RUNNING)) {
        stateSeen = true;
        break;
      }
    }
  } while (!stateSeen);

  state_listeners_--;

  return res;
}

int32_t Camera3DeviceClient::Prepare(int streamId) {
  int32_t res = 0;
  pthread_mutex_lock(&lock_);

  Camera3Stream *stream;
  int32_t outputStreamIdx = streams_.indexOfKey(streamId);
  if (-ENOENT == outputStreamIdx) {
      CAMERA_ERROR("%s: Stream %d is invalid!\n", __func__, streamId);
      res = -EINVAL;
  }

  stream = streams_.editValueAt(outputStreamIdx);
  if (stream->IsStreamActive()) {
    CAMERA_ERROR("%s: Stream %d has already received requests\n", __func__,
               streamId);
    res = -EINVAL;
    goto exit;
  }

  if (request_handler_.IsStreamActive(*stream)) {
    CAMERA_ERROR("%s: Stream %d already has pending requests\n", __func__,
               streamId);
    res = -EINVAL;
    goto exit;
  }

  res = prepare_handler_.Prepare(stream);

exit:

  pthread_mutex_unlock(&lock_);

  return res;
}

int32_t Camera3DeviceClient::TearDown(int streamId) {
  int32_t res = 0;
  pthread_mutex_lock(&lock_);

  Camera3Stream *stream;
  int32_t outputStreamIdx = streams_.indexOfKey(streamId);
  if (-ENOENT == outputStreamIdx) {
      CAMERA_ERROR("%s: Stream %d is invalid!\n", __func__, streamId);
      res = -EINVAL;
  }

  stream = streams_.editValueAt(outputStreamIdx);
  if (request_handler_.IsStreamActive(*stream)) {
    CAMERA_ERROR("%s: Stream %d already has pending requests\n", __func__,
               streamId);
    res = -EINVAL;
    goto exit;
  }

  res = stream->TearDown();

exit:

  pthread_mutex_unlock(&lock_);

  return res;
}

ScopedAStatus Camera3DeviceClient::processCaptureResult(
    const std::vector<::aidl::android::hardware::camera::device::CaptureResult>& results) {
  for (const ::aidl::android::hardware::camera::device::CaptureResult &result : results) {
    HandleCaptureResult(result);
  }
  return ScopedAStatus::ok();
}

ScopedAStatus Camera3DeviceClient::notify(const std::vector<NotifyMsg>& messages) {
  size_t count = messages.size();
  for (size_t i = 0; i < count; i++) {
      const NotifyMsg& notify_msg = messages[i];
      switch (notify_msg.getTag()) {
          case NotifyMsg::Tag::error:
              NotifyError(notify_msg.get<NotifyMsg::Tag::error>());
              break;
          case NotifyMsg::Tag::shutter:
              NotifyShutter(notify_msg.get<NotifyMsg::Tag::shutter>());
              break;
          default:
            SET_ERR("Unknown notify message from HAL");
      }
  }
  return ScopedAStatus::ok();
}

  StreamConfigurationMode Camera3DeviceClient::GetOpMode() {
  CAMERA_DEBUG("%s: Enter: \n", __func__);

  uint32_t operation_mode = 0x00;

  // Handle ZZHDR Mode
  if (cam_feature_flags_ & static_cast<uint32_t>(CamFeatureFlag::kHDR)) {
    operation_mode |= QCAMERA3_SENSORMODE_ZZHDR_OPMODE;
  }

  // Handle EIS mode
  if (cam_feature_flags_ & static_cast<uint32_t>(CamFeatureFlag::kEIS)) {
    operation_mode |= EIS_ENABLE;
  }
  // Handle LDC mode
  if (cam_feature_flags_ & static_cast<uint32_t>(CamFeatureFlag::kLDC)) {
    operation_mode |= LDC_ENABLE;
  }
  // Handle LCAC mode
  if (cam_feature_flags_ & static_cast<uint32_t>(CamFeatureFlag::kLCAC)) {
    operation_mode |= LCAC_ENABLE;
  }

  CAMERA_DEBUG("%s: Exit: \n", __func__);

  return static_cast<StreamConfigurationMode>(operation_mode);
}

}  // namespace adaptor ends here

}  // namespace camera ends here
