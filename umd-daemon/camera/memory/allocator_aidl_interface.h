/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "camera_memory_interface.h"

#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>

class AidlAllocUsage : public IMemAllocUsage {
 public:
  uint64_t ToLocal(int32_t common) const;
  uint64_t ToLocal(MemAllocFlags common) const;
  MemAllocFlags ToCommon(uint64_t local) const;

 private:
  static const std::unordered_map<int32_t, uint64_t> usage_flag_map_;
};

class AidlAllocBuffer : public IBufferInterface {
 public:
  AidlAllocBuffer() : native_handle_(nullptr), stride_(0),
                      width_(0), height_(0) {};
  ~AidlAllocBuffer(){};
  buffer_handle_t & GetNativeHandle();
  int GetFD() override;
  PixelFormat GetFormat() override;
  uint32_t GetSize() override;
  uint32_t GetWidth() override;
  uint32_t GetHeight() override;
  uint32_t GetStride() override;

  uint32_t stride_;
  int32_t width_;
  int32_t height_;
  PixelFormat format_;

 private:
  buffer_handle_t native_handle_;
};

class AidlAllocDevice : public IAllocDevice {
 public:
  AidlAllocDevice();
  ~AidlAllocDevice();

  bool IsValid() const;

  MemAllocError AllocBuffer(IBufferHandle& handle, int32_t width,
                            int32_t height, int32_t format,
                            MemAllocFlags usage, uint32_t* stride) override;

  MemAllocError ImportBuffer(IBufferHandle& handle,
                             void* buffer_handle) override;

  MemAllocError FreeBuffer(IBufferHandle handle) override;

  MemAllocError Perform(const IBufferHandle& handle, AllocDeviceAction action,
                        void* result) override;

  MemAllocError MapBuffer(const IBufferHandle& handle, int32_t start_x,
                          int32_t start_y, int32_t width, int32_t height,
                          MemAllocFlags usage, void** vaddr) override;

  MemAllocError UnmapBuffer(const IBufferHandle& handle) override;

 private:
  bool valid_;
};
