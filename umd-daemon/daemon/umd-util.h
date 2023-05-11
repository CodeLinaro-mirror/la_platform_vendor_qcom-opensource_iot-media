/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#pragma once

#include <cutils/properties.h>
#include <cutils/uevent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <type_traits>

enum AudioState {
  AUDIO_STATE_INVALID,
  AUDIO_STATE_PLAYBACK,
  AUDIO_STATE_CAPTURE,
  AUDIO_STATE_PLAYBACK_CAPTURE,
  AUDIO_STATE_PAUSED
};

enum {
  C_STATUS = 0,
  P_STATUS = 1,
};

enum State {
  START,
  STOP
};

typedef std::function<void(std::vector<uint8_t> data)> AudioCallback;
typedef std::function<void(AudioState status)> EventCallback;
typedef std::function<void(AudioState status, EventCallback uevent_cb)>
  UmdEventCallback;

struct UeventData {
  int32_t fd;
  AudioState cur;
};

class UmdUtil {
 public:
  UmdUtil();
  ~UmdUtil();

  int32_t monitor_audio_status(EventCallback uevent_cb,
                               UmdEventCallback umd_event_cb);

 private:
  bool mActive;
};

class Property {
 public:
  template <typename T>
  static T Get(std::string property, T default_value) {
    T value;
    char prop_val[PROPERTY_VALUE_MAX];

    std::stringstream s;
    s << default_value;

    property_get(property.c_str(), prop_val, s.str().c_str());
    std::stringstream output(prop_val);
    if constexpr (std::is_same_v<T, const char *>)
      value = output.str().c_str();
    else
      output >> value;
    return value;
  }
};
