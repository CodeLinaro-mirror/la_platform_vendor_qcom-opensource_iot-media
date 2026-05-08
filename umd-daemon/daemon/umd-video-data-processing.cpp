/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "umd-video-data-processing.h"

#include <hardware/camera3.h>
#define LOG_TAG "UmdVideoData"

#include "umd-logging.h"
#ifndef JPEG_BLOB_OFFSET
#define JPEG_BLOB_OFFSET (0)
#endif
#include <C2AllocatorGralloc.h>
#include <C2BlockInternal.h>
#include <C2PlatformSupport.h>
#include <gralloc_priv.h>

#define C2_COMPONENT_NAME "c2.qti.avc.encoder"
#define C2_RATE_CTRL_DISABLE 0x7F000000
#define C2_BITRATE 10000000

#define UMD_VIDEO_CTRL_GET_PAN(X)    (((int32_t *)(&(X)))[0] / 3600)
#define UMD_VIDEO_CTRL_GET_TILT(X)   (((int32_t *)(&(X)))[1] / 3600)
#define UMD_VIDEO_CTRL_SET_PAN_AND_TILT(P, T) \
    ((((signed long)(P) * 3600) & 0xFFFFFFFF) | \
    ((((signed long)(T) * 3600) & 0xFFFFFFFF) << 32))

const uint32_t VIDEO_BUFFER_TIMEOUT = 1000;  // [ms]
const uint32_t C2_OUT_FRAMERATE = 30;
const uint32_t C2_PFRAME_VALUE = 29;
const uint32_t C2_BFRAME_VALUE = 0;
const uint32_t C2_REFRESH_PERIOD = 0;
const uint64_t FPS_TIME_INTERVAL = 3000000;
std::unique_ptr<UmdBufferMap> buffMap;

typedef std::function<void(uint8_t* data, uint32_t size, uint64_t timestamp, StreamBuffer &buffer)>
  UmdFrameCallback;
typedef std::function<void(uint64_t frameNumber)>UmdFrameDropCallback;
class UmdC2Notifier : public IC2Notifier {
 public:
  UmdC2Notifier(UmdFrameCallback frameCb, UmdFrameDropCallback frameDropCb) : mFrameCb(frameCb),
     mFrameDropCb(frameDropCb) {}
  void EventHandler(C2EventType event, void* payload) override {
    switch (event) {
      case C2EventType::kError:
        UMD_LOG_ERROR ("Received engine error\n");
        break;
      case C2EventType::kEOS:
        break;
      case C2EventType::kDrop:
        UMD_LOG_INFO ("Drop event received\n");
        mFrameDropCb(*static_cast<uint64_t*>(payload));
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
    mFrameCb((uint8_t*)view.data(), block.size(), timestamp, buffMap->find(index));
    buffMap->erase(index);
  }

 private:
  UmdFrameCallback mFrameCb;
  UmdFrameDropCallback mFrameDropCb;
};

class H264DataProcessor : public UmdVideoData {
public:
  H264DataProcessor(UmdGadget *gadget,C2Module *C2Module, sp<Camera3DeviceClient> deviceClient,
      UmdVideoSetup setup) {
    mGadget = gadget;
    mC2Module = C2Module;
    mDeviceClient = deviceClient;
    mSetup = setup;
  }
private:
  struct timespec mTv;
  struct timespec mPrevtv;
  int64_t mCount;
  UmdVideoSetup mSetup;
  MessageQ<std::pair<StreamBuffer, int32_t>> mCodecVideoBufferQueue;


  bool init() override {
    UMD_LOG_INFO("Initializing H264 processor");
    mActive = true;
    mTv = {0, 0};
    mPrevtv = {0, 0};
    mCount = 0;
    mAllocDeviceInterface = AllocDeviceFactory::CreateAllocDevice();
    if (nullptr == mAllocDeviceInterface) {
      UMD_LOG_ERROR("Alloc device creation failed!\n");
      return false;
    }
    if (!InitializeCodec(mC2Module))
      return false;

    return true;
  }

  void processData(StreamBuffer buffer) override {
    // Data Processing specific to H264 format
    uint8_t *mapped_buffer = nullptr;
    uint32_t id =  (buffer.stream_id << 29) | (buffer.frame_number & 0x1FFFFFFF);
    MemAllocFlags usage;
    MemAllocError ret;
    usage.flags = IMemAllocUsage::kSwReadOften;
    if (mActive) {
      ret = mAllocDeviceInterface->MapBuffer(
          buffer.handle, 0,
          0, buffer.info.plane_info[0].width,
          buffer.info.plane_info[0].height,
          usage, (void **)&mapped_buffer);

      if ((MemAllocError::kAllocOk != ret) || (NULL == mapped_buffer)) {
        UMD_LOG_ERROR("%s: Unable to map buffer: %p res: %d\n", __func__,
            mapped_buffer, ret);
        mDeviceClient->ReturnStreamBuffer(buffer);
        return;
      }

      std::shared_ptr<C2Buffer> c2buffer;
      uint64_t timestamp = buffer.timestamp;
      uint32_t flags = 0;
      uint32_t index = id;
      std::list<std::unique_ptr<C2Param>> settings;

      c2buffer = ImportGraphicBuffer(buffer);
      if (c2buffer == nullptr) {
        UMD_LOG_ERROR("Failed to create c2buffer\n");
        mAllocDeviceInterface->UnmapBuffer(buffer.handle);
        mDeviceClient->ReturnStreamBuffer(buffer);
        return;
      }

      buffMap->insert(id, buffer);
      mC2Module->Queue(c2buffer, settings, index, timestamp, flags);
      return;
    }
    mDeviceClient->ReturnStreamBuffer(buffer);
  }

  bool deinit() override {
    UMD_LOG_INFO("Deinitializing H264 processor");
    mActive = false;

    // Stop codec2 module first
    if (mC2Module) {
      mC2Module->Stop();
      delete mC2Module;
      mC2Module = nullptr;
    }

    // Stop video buffer thread
    if (mVideoBufferThread) {
      mCodecVideoBufferQueue.abort();
      mVideoBufferThread->join();
      mVideoBufferThread = nullptr;
    }

    // Reset codec video buffer queue
    mCodecVideoBufferQueue.reset();

    if (nullptr != mAllocDeviceInterface) {
      AllocDeviceFactory::DestroyAllocDevice(mAllocDeviceInterface);
      mAllocDeviceInterface = nullptr;
    }

    return true;
  }

  bool InitializeCodec(C2Module *mC2Module) {
    // Initialize codec2 component
    buffMap = std::make_unique<UmdBufferMap>();
    UmdFrameCallback umdFrameCb = [&](uint8_t* data, uint32_t size, uint64_t
    timestamp, StreamBuffer &buffer) {
    if (mActive) {
      uint32_t bufidx = umd_gadget_submit_buffer (mGadget, UMD_VIDEO_STREAM_ID,
          data, size, size, timestamp);
      PrintFPS();
      mCodecVideoBufferQueue.push(std::make_pair (buffer, bufidx));
    } else {
      mAllocDeviceInterface->UnmapBuffer(buffer.handle);
      mDeviceClient->ReturnStreamBuffer(buffer);
    }
  };

  UmdFrameDropCallback umdFrameDropCb = [&](uint64_t frameNum) {
    StreamBuffer buffer = buffMap->find(frameNum);
    buffMap->erase(frameNum);
    mAllocDeviceInterface->UnmapBuffer(buffer.handle);
    mDeviceClient->ReturnStreamBuffer(buffer);
  };

    std::shared_ptr<IC2Notifier> notifier = std::make_shared<UmdC2Notifier>(
    umdFrameCb,umdFrameDropCb);
    mC2Module->Initialize(notifier);

    // Set the encoder parameters
    SetEncoderParameters();

    // Start c2 component
    if (mC2Module->Start()) {
      UMD_LOG_ERROR("Failed to start c2module\n");
      delete mC2Module;
      return false;
    }

    mVideoBufferThread = std::unique_ptr<std::thread>(
        new std::thread(&H264DataProcessor::codecVideoBufferLoop, this));

    if (nullptr == mVideoBufferThread) {
      UMD_LOG_ERROR("Codec video buffer thread creation failed!\n");
      delete mC2Module;
      return false;
    }

    return true;
  }

  void codecVideoBufferLoop() {
    while (mActive || mCodecVideoBufferQueue.size()) {
      int32_t bufidx;
      std::pair<StreamBuffer, int32_t> buffer_pair;
      if (!mCodecVideoBufferQueue.pop(buffer_pair)) {
        StreamBuffer buffer = buffer_pair.first;
        int32_t bufidx = buffer_pair.second;
        umd_gadget_wait_buffer (mGadget, UMD_VIDEO_STREAM_ID, bufidx);
        UMD_LATENCY_LOG(
            "UmdCamera-latency: FrameNumber: %d fd: %d Return buffer"
            " from UMD \n",
            buffer.frame_number, buffer.fd);
        if (buffer.handle == nullptr) {
          UMD_LOG_ERROR("Invalid buffer handle\n");
          continue;
        }
        mAllocDeviceInterface->UnmapBuffer(buffer.handle);
        mDeviceClient->ReturnStreamBuffer(buffer);
      }
    }
    UMD_LOG_INFO("codecVideoBufferLoop terminate!\n");
  }

  void SetParams(std::unique_ptr<C2Param> c2param, std::string type) {
    try {
      mC2Module->SetParam(c2param);
      UMD_LOG_INFO("Successfully set parameter: %s", type.c_str());
    } catch (std::exception &e) {
      UMD_LOG_ERROR("Failed to set c2module parameter, error: '%s'!", e.what());
    }
  }

  void SetEncoderParameters() {
    std::unique_ptr<C2Param> c2param;

    // input format
    C2StreamPixelFormatInfo::input pixformat;
    pixformat.value = static_cast<uint32_t>(C2PixelFormat::kNV12);
    SetParams(C2Param::Copy(pixformat), C2_PARAMKEY_PIXEL_FORMAT);

    // input resolution
    C2StreamPictureSizeInfo::input dimensions;
    dimensions.width = mSetup.width;
    dimensions.height = mSetup.height;
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
    ratectrl.value = static_cast<C2Config::bitrate_mode_t>
        (C2Config::BITRATE_VARIABLE);
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

    // set realtime session
    C2RealTimePriorityTuning priority;
    priority.value = 0;
    SetParams(C2Param::Copy(priority), C2_PARAMKEY_PRIORITY);
  }

  void PrintFPS() {
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

  std::shared_ptr<C2Buffer> ImportGraphicBuffer(StreamBuffer buffer) {
    uint64_t format = HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS;
    std::unique_ptr<private_handle_t> privateHandle;
    uint32_t height = buffer.info.plane_info[0].height;
    uint32_t width = buffer.info.plane_info[0].width;
    uint32_t stride = buffer.info.plane_info[0].stride;
    uint64_t usage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                     GRALLOC_USAGE_SW_READ_OFTEN;

    const private_handle_t *priv_handle =
        static_cast<const private_handle_t *>(GetAllocBufferHandle(buffer.handle));
    if (priv_handle == NULL) {
      UMD_LOG_ERROR("Failed to create private_handle_t");
      return nullptr;
    }

    C2Handle *handle = android::WrapNativeCodec2GrallocHandle(
        (native_handle_t *)priv_handle, width, height, format, usage, stride);
    if (handle == nullptr) {
      UMD_LOG_ERROR("Failed to create C2 handle");
      return nullptr;
    }

    std::shared_ptr<C2Allocator> allocator;
    std::shared_ptr<C2AllocatorStore> store =
        android::GetCodec2PlatformAllocatorStore();
    auto ret = store->fetchAllocator(
        android::C2PlatformAllocatorStore::DEFAULT_GRAPHIC, &allocator);
    if (ret != C2_OK || allocator == nullptr) {
      UMD_LOG_ERROR("Failed to create C2 allocator");
      delete handle;
      return nullptr;
    }

    std::shared_ptr<C2GraphicAllocation> allocation;
    ret = allocator->priorGraphicAllocation(handle, &allocation);
    if (ret != C2_OK) {
      UMD_LOG_ERROR("Prior Graphic allocation failed");
      delete handle;
      return nullptr;
    }

    std::shared_ptr<C2GraphicBlock> block =
        _C2BlockFactory::CreateGraphicBlock(allocation);
    if (!block) {
      UMD_LOG_ERROR("Failed to create graphic block!");
      return nullptr;
    }

    auto c2buffer = C2Buffer::CreateGraphicBuffer(
        block->share(C2Rect(block->width(), block->height()), ::C2Fence()));
    if (!c2buffer) {
      UMD_LOG_ERROR("Failed to create graphic C2 buffer!");
      return nullptr;
    }

    return c2buffer;
  }

};

class MjpegDataProcessor : public UmdVideoData {
public:
  MjpegDataProcessor(UmdGadget *gadget, sp<Camera3DeviceClient> deviceClient) {
    mGadget = gadget;
    mDeviceClient = deviceClient;
  }

private:
  MessageQ<std::pair<StreamBuffer, int32_t>> mVideoBufferQueue = VIDEO_BUFFER_TIMEOUT;

  bool init() override {
    UMD_LOG_INFO("Initializing MJPEG processor");
    mActive = true;
    mAllocDeviceInterface = AllocDeviceFactory::CreateAllocDevice();
    if (nullptr == mAllocDeviceInterface) {
      UMD_LOG_ERROR("Alloc device creation failed!\n");
      return false;
    }
    mVideoBufferThread = std::unique_ptr<std::thread>(
        new std::thread(&MjpegDataProcessor::videoBufferLoop, this));

    if (nullptr == mVideoBufferThread) {
      UMD_LOG_ERROR("Video buffer thread creation failed!\n");
      return false;
    }

    return true;
  }

  void processData(StreamBuffer buffer) override {
    int maxsize = 0;
    int size = 0;
    uint8_t *mapped_buffer = nullptr;
    MemAllocFlags usage;
    MemAllocError ret;
    usage.flags = IMemAllocUsage::kSwReadOften;
    // Processing specific to MJPEG format
    if (mActive) {
      ret = mAllocDeviceInterface->MapBuffer(
          buffer.handle, 0,
          0, 0,
          0,
          usage, (void **)&mapped_buffer);

      if ((MemAllocError::kAllocOk != ret) || (NULL == mapped_buffer)) {
        UMD_LOG_ERROR("%s: Unable to map buffer: %p res: %d\n", __func__,
            mapped_buffer, ret);
        mDeviceClient->ReturnStreamBuffer(buffer);
        return;
      }

      maxsize = buffer.info.plane_info[0].size;
      size = GetBlobSize(mapped_buffer, buffer.info.plane_info[0].size);
      uint32_t bufidx = umd_gadget_submit_buffer(mGadget, UMD_VIDEO_STREAM_ID,
          mapped_buffer, size, maxsize, buffer.timestamp);
      if (bufidx < 0) {
        mAllocDeviceInterface->UnmapBuffer(buffer.handle);
        return;
      }

      mVideoBufferQueue.push(std::make_pair(buffer, bufidx));
      return;
    }
   // mAllocDeviceInterface->UnmapBuffer(buffer.handle);
    mDeviceClient->ReturnStreamBuffer(buffer);
  }

  bool deinit() override {
    UMD_LOG_INFO("Deinitializing MJPEG processor");
    mActive = false;

    if (nullptr != mAllocDeviceInterface) {
      AllocDeviceFactory::DestroyAllocDevice(mAllocDeviceInterface);
    }
    if (mVideoBufferThread == nullptr) {
      UMD_LOG_ERROR("Video loop thread not started!\n");
      return -EINVAL;
    }
    mVideoBufferQueue.abort();

    mVideoBufferThread->join();
    mVideoBufferThread = nullptr;
    mVideoBufferQueue.reset();
    return true;
  }

  void videoBufferLoop() {
    while (mActive || mVideoBufferQueue.size()) {
      std::pair<StreamBuffer, int32_t> buffer_pair;
      if (!mVideoBufferQueue.pop(buffer_pair)) {
        StreamBuffer buffer = buffer_pair.first;
        int32_t bufidx = buffer_pair.second;
        umd_gadget_wait_buffer(mGadget, UMD_VIDEO_STREAM_ID, bufidx);
        UMD_LATENCY_LOG(
            "UmdCamera-latency: FrameNumber: %d fd: %d Return buffer"
            " from UMD \n",
            buffer.frame_number, buffer.fd);
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

  uint32_t GetBlobSize(uint8_t *buffer, uint32_t size) {
    uint32_t bsize = sizeof(struct camera3_jpeg_blob);
    uint32_t res = size;

    if (size > bsize) {
      uint8_t *footer = buffer + size - bsize - JPEG_BLOB_OFFSET;
      struct camera3_jpeg_blob *blob = (struct camera3_jpeg_blob *)footer;

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
};

class Yuy2DataProcessor : public UmdVideoData {
public:
  Yuy2DataProcessor(UmdGadget *gadget, sp<Camera3DeviceClient> deviceClient) {
    mGadget = gadget;
    mDeviceClient = deviceClient;
  }

private:
  MessageQ<std::pair<StreamBuffer, int32_t>> mVideoBufferQueue = VIDEO_BUFFER_TIMEOUT;

  bool init() override {
    UMD_LOG_INFO("Initializing YUY2 processor");
    mActive = true;
    mAllocDeviceInterface = AllocDeviceFactory::CreateAllocDevice();
    if (nullptr == mAllocDeviceInterface) {
      UMD_LOG_ERROR("Alloc device creation failed!\n");
      return false;
    }
    mVideoBufferThread = std::unique_ptr<std::thread>(
        new std::thread(&Yuy2DataProcessor::videoBufferLoop, this));

    if (nullptr == mVideoBufferThread) {
      UMD_LOG_ERROR("Video buffer thread creation failed!\n");
      return false;
    }

    return true;
  }

  void processData(StreamBuffer buffer) override {
    int maxsize = 0;
    int size = 0;
    uint8_t *mapped_buffer = nullptr;
    MemAllocFlags usage;
    MemAllocError ret;
    // Processing specific to YUY2 format
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
        mDeviceClient->ReturnStreamBuffer(buffer);
        return;
      }

      size = buffer.info.plane_info[0].stride * buffer.info.plane_info[0].height * 2;
      uint32_t bufidx = umd_gadget_submit_buffer(mGadget, UMD_VIDEO_STREAM_ID,
          mapped_buffer, size, maxsize, buffer.timestamp);
      if (bufidx < 0) {
        mAllocDeviceInterface->UnmapBuffer(buffer.handle);
        return;
      }
      mVideoBufferQueue.push(std::make_pair(buffer, bufidx));
      return;
    }
   // mAllocDeviceInterface->UnmapBuffer(buffer.handle);
    mDeviceClient->ReturnStreamBuffer(buffer);
  }

  bool deinit() override {
    UMD_LOG_INFO("Deinitializing YUY2 processor");
    mActive = false;
    if (nullptr != mAllocDeviceInterface) {
      AllocDeviceFactory::DestroyAllocDevice(mAllocDeviceInterface);
    }
    if (mVideoBufferThread == nullptr) {
      UMD_LOG_ERROR("Video loop thread not started!\n");
      return -EINVAL;
    }
    mVideoBufferQueue.abort();

    mVideoBufferThread->join();
    mVideoBufferThread = nullptr;

    mVideoBufferQueue.reset();
    return true;
  }

  void videoBufferLoop() {
    while (mActive || mVideoBufferQueue.size()) {
      std::pair<StreamBuffer, int32_t> buffer_pair;
      if (!mVideoBufferQueue.pop(buffer_pair)) {
        StreamBuffer buffer = buffer_pair.first;
        int32_t bufidx = buffer_pair.second;
        umd_gadget_wait_buffer(mGadget, UMD_VIDEO_STREAM_ID, bufidx);
        UMD_LATENCY_LOG(
            "UmdCamera-latency: FrameNumber: %d fd: %d Return buffer"
            " from UMD \n",
            buffer.frame_number, buffer.fd);
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
};

// Factory method implementation
UmdVideoData *UmdVideoDataFactory ::createUmdVideoInstance(UmdVideoSetup mVsetup,
                                                           UmdGadget *gadget,
                                                           C2Module *C2Module,
                                                           sp<Camera3DeviceClient> deviceClient) {
  switch (mVsetup.format) {
    case UMD_VIDEO_FMT_H264:
      return new H264DataProcessor(gadget,C2Module, deviceClient, mVsetup);
    case UMD_VIDEO_FMT_MJPEG:
      return new MjpegDataProcessor(gadget, deviceClient);
    case UMD_VIDEO_FMT_YUYV:
      return new Yuy2DataProcessor(gadget, deviceClient);
    default:
      return nullptr;  // Unsupported format
  }
}
