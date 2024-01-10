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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <mutex>
#include <queue>
#include <condition_variable>

template <typename T>
class MessageQ {
public:
  MessageQ()
      : mTimeout(0),
        mAbortFlag(false) {}

  MessageQ(int32_t timeout)
      : mTimeout (timeout),
        mAbortFlag(false) {}

  void push(T obj) {
    std::lock_guard<std::mutex> lock(mMutex);
    mQueue.push(obj);
    mCondition.notify_all();
  }

  int32_t pop(T &obj) {
    std::unique_lock<std::mutex> lock(mMutex);
    if (mTimeout) {
      auto now = std::chrono::system_clock::now();
      auto timeout = now + std::chrono::milliseconds(mTimeout);
      mCondition.wait_until(lock, timeout,
          [this, timeout] {
            return (mQueue.size() > 0) ||
                (timeout <= std::chrono::system_clock::now()) || mAbortFlag;
          });
      if (mQueue.size() == 0) {
        return -1;
      }
    } else {
      mCondition.wait(lock,
          [this] {
            return (mQueue.size() > 0) || mAbortFlag;
          });
      if (mQueue.size() == 0) {
        return -1;
      }
    }
    obj = mQueue.front();
    mQueue.pop();
    return 0;
  }

  int32_t size() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue.size();
  }

  void abort() {
    std::lock_guard<std::mutex> lock(mMutex);
    mAbortFlag = true;
    mCondition.notify_all();
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mMutex);
    mAbortFlag = false;
  }

private:
  std::queue<T> mQueue;
  std::condition_variable mCondition;
  std::mutex mMutex;
  int32_t mTimeout;
  std::atomic<bool> mAbortFlag;
};