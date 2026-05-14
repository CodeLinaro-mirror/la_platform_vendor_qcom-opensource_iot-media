/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "allocator_aidl_interface.h"
#include "utils/camera_log.h"

using ::android::GraphicBufferAllocator;
using ::android::GraphicBufferMapper;
using ::android::Rect;
using ::android::status_t;
using ::android::OK;

const std::unordered_map<int32_t, uint64_t> AidlAllocUsage::usage_flag_map_ = {
    {IMemAllocUsage::kSwReadOften,   static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN)},
    {IMemAllocUsage::kVideoEncoder,  static_cast<uint64_t>(BufferUsage::VIDEO_ENCODER)},
    {IMemAllocUsage::kHwTexture,     static_cast<uint64_t>(BufferUsage::GPU_TEXTURE)},
    {IMemAllocUsage::kHwComposer,    static_cast<uint64_t>(BufferUsage::COMPOSER_CURSOR)},
    {IMemAllocUsage::kHwCameraRead,  static_cast<uint64_t>(BufferUsage::CAMERA_INPUT)},
    {IMemAllocUsage::kHwCameraWrite, static_cast<uint64_t>(BufferUsage::CAMERA_OUTPUT)},
    {IMemAllocUsage::kHwRender,      static_cast<uint64_t>(BufferUsage::GPU_RENDER_TARGET)}};

uint64_t AidlAllocUsage::ToLocal(int32_t common) const {
  uint64_t local_usage = 0;
  for (auto &it : usage_flag_map_) {
    if (it.first & common) {
      local_usage |= it.second;
    }
  }
  return local_usage;
}

uint64_t AidlAllocUsage::ToLocal(MemAllocFlags common) const {
  uint64_t local_usage = 0;
  for (auto &it : usage_flag_map_) {
    if (it.first & common.flags) {
      local_usage |= it.second;
    }
  }
  return local_usage;
}

MemAllocFlags AidlAllocUsage::ToCommon(uint64_t local) const {
  MemAllocFlags common;
  common.flags = 0;
  for (auto &it : usage_flag_map_) {
    if (it.second & local) {
      common.flags |= it.first;
    }
  }
  return common;
}

buffer_handle_t &AidlAllocBuffer::GetNativeHandle() { return native_handle_; }

int AidlAllocBuffer::GetFD() {
  if (native_handle_ == nullptr) {
    return -1;
  }
  return native_handle_->data[0];
}

PixelFormat AidlAllocBuffer::GetFormat() {
  return format_;
}

uint32_t AidlAllocBuffer::GetSize() {
  return 0;
}

uint32_t AidlAllocBuffer::GetWidth() {
  return width_;
}

uint32_t AidlAllocBuffer::GetHeight() {
  return height_;
}

uint32_t AidlAllocBuffer::GetStride() {
  return stride_;
}

AidlAllocDevice::AidlAllocDevice() : valid_(false) {
  // GraphicBufferAllocator and GraphicBufferMapper are singletons backed by
  // the AIDL IAllocator / IMapper (Mapper5) on Android 16+.
  // Accessing the singletons triggers lazy initialisation of the underlying
  // AIDL services; no hwservicemanager is required.
  (void)GraphicBufferMapper::get();
  (void)GraphicBufferAllocator::get();
  valid_ = true;
  CAMERA_INFO("%s: AidlAllocDevice initialised (GraphicBufferAllocator/Mapper)", __func__);
}

AidlAllocDevice::~AidlAllocDevice() {}

bool AidlAllocDevice::IsValid() const {
  return valid_;
}

MemAllocError AidlAllocDevice::AllocBuffer(IBufferHandle& handle, int32_t width,
                                           int32_t height, int32_t format,
                                           MemAllocFlags usage,
                                           uint32_t *stride) {
  assert(width && height);

  uint64_t local_usage = AidlAllocUsage().ToLocal(usage);

  AidlAllocBuffer *buffer = new AidlAllocBuffer;
  handle = buffer;

  buffer_handle_t buf_handle = nullptr;
  uint32_t buf_stride = 0;

  status_t ret = GraphicBufferAllocator::get().allocate(
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
      static_cast<android::PixelFormat>(format),
      1u /* layerCount */,
      local_usage,
      &buf_handle,
      &buf_stride,
      "umd-camera");

  if (ret != OK || buf_handle == nullptr) {
    CAMERA_ERROR("%s: Buffer allocation failed, status=%d\n", __func__, ret);
    delete buffer;
    handle = nullptr;
    return MemAllocError::kAllocFail;
  }

  buffer->GetNativeHandle() = buf_handle;
  buffer->format_ = static_cast<PixelFormat>(format);
  buffer->width_  = width;
  buffer->height_ = height;
  buffer->stride_ = buf_stride;
  *stride = buf_stride;

  return MemAllocError::kAllocOk;
}

MemAllocError AidlAllocDevice::ImportBuffer(IBufferHandle& handle,
                                            void* buffer_handle) {
  CAMERA_ERROR("%s: Not implemented", __func__);
  assert(0);
  return MemAllocError::kAllocOk;
}

MemAllocError AidlAllocDevice::FreeBuffer(IBufferHandle handle) {
  if (nullptr != handle) {
    AidlAllocBuffer *b = static_cast<AidlAllocBuffer *>(handle);
    assert(b != nullptr);
    const native_handle_t *buf = b->GetNativeHandle();
    if (buf != nullptr) {
      status_t ret = GraphicBufferAllocator::get().free(buf);
      if (ret != OK) {
        CAMERA_ERROR("%s: Failed to free buffer, status=%d\n", __func__, ret);
      }
    }
    delete handle;
    handle = nullptr;
  }
  return MemAllocError::kAllocOk;
}

MemAllocError AidlAllocDevice::MapBuffer(const IBufferHandle& handle,
                                         int32_t start_x, int32_t start_y,
                                         int32_t width, int32_t height,
                                         MemAllocFlags usage, void **vaddr) {
  AidlAllocBuffer *b = static_cast<AidlAllocBuffer *>(handle);
  assert(b != nullptr);

  uint64_t local_usage = AidlAllocUsage().ToLocal(usage);
  Rect bounds(start_x, start_y, start_x + width, start_y + height);

  status_t ret = GraphicBufferMapper::get().lock(
      b->GetNativeHandle(),
      local_usage,
      bounds,
      vaddr);

  if (ret != OK) {
    CAMERA_ERROR("%s: Failed to lock buffer, status=%d\n", __func__, ret);
    *vaddr = nullptr;
    return MemAllocError::kAllocFail;
  }

  return MemAllocError::kAllocOk;
}

MemAllocError AidlAllocDevice::UnmapBuffer(const IBufferHandle& handle) {
  AidlAllocBuffer *b = static_cast<AidlAllocBuffer *>(handle);
  assert(b != nullptr);

  status_t ret = GraphicBufferMapper::get().unlock(b->GetNativeHandle());
  if (ret != OK) {
    CAMERA_ERROR("%s: Failed to unlock buffer, status=%d\n", __func__, ret);
    return MemAllocError::kAllocFail;
  }

  return MemAllocError::kAllocOk;
}

MemAllocError AidlAllocDevice::Perform(const IBufferHandle& handle,
                                       AllocDeviceAction action, void* result) {
  AidlAllocBuffer *b = static_cast<AidlAllocBuffer *>(handle);
  if (nullptr == b) {
    return MemAllocError::kAllocFail;
  }

  switch (action) {
    case AllocDeviceAction::GetHeight:
      *static_cast<int32_t*>(result) = handle->GetHeight();
      return MemAllocError::kAllocOk;
    case AllocDeviceAction::GetStride:
      *static_cast<int32_t*>(result) = handle->GetStride();
      return MemAllocError::kAllocOk;
    case AllocDeviceAction::GetAlignedHeight:
      // Align to multiple of 64
      *static_cast<int32_t*>(result) = (handle->GetHeight() + 0x3F) & (~0x3F);
      return MemAllocError::kAllocOk;
    case AllocDeviceAction::GetAlignedWidth:
      *static_cast<int32_t*>(result) = handle->GetStride();
      return MemAllocError::kAllocOk;
    default:
      CAMERA_ERROR("%s: Unrecognized action to perform.", __func__);
      return MemAllocError::kAllocFail;
  }
}
