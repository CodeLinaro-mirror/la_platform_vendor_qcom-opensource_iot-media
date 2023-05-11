/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "umd-util.h"
#include <sys/epoll.h>
#include "umd-logging.h"

#define LOG_TAG "UmdUtil"

#define UEVENT_MSG_LEN 2048
#define C_STATUS_FILE "/config/usb_gadget/g1/functions/uac2.0/c_status"
#define P_STATUS_FILE "/config/usb_gadget/g1/functions/uac2.0/p_status"
#define UEVENT_MSG "change@/devices/virtual/android_usb/android0/f_uac2"

UmdUtil::UmdUtil() {
  mActive = false;
}

UmdUtil::~UmdUtil() {
  mActive = false;
}

static char get_audio_sysfs_data(int32_t sysfs_entry) {
  int32_t fd = 0;
  char buffer[UEVENT_MSG_LEN] = {0};
  ssize_t length = 0;

  switch (sysfs_entry) {
    case C_STATUS:
      fd = open(C_STATUS_FILE, O_RDONLY);
      if (fd == -1)
        UMD_LOG_ERROR("Open of %s failed\n", C_STATUS_FILE);
      length = read(fd, buffer, sizeof(buffer) - 1);
      if (length < 0)
        UMD_LOG_ERROR("Read of %s failed\n", C_STATUS_FILE);
      break;
    case P_STATUS:
      fd = open(P_STATUS_FILE, O_RDONLY);
      if (fd == -1)
        UMD_LOG_ERROR("Open of %s failed\n", P_STATUS_FILE);
      length = read(fd, buffer, sizeof(buffer) - 1);
      if (length < 0)
        UMD_LOG_ERROR("Read of %s failed\n", P_STATUS_FILE);
      break;
  }
  return buffer[0];
}

static AudioState get_audio_client_status() {
  char c_status = '\0', p_status = '\0';
  AudioState ret = AUDIO_STATE_INVALID;

  p_status = get_audio_sysfs_data(P_STATUS);
  c_status = get_audio_sysfs_data(C_STATUS);
  if (c_status == '0' && p_status == '0')
    ret = AUDIO_STATE_PAUSED;
  if (c_status == '1' && p_status == '1')
    ret = AUDIO_STATE_PLAYBACK_CAPTURE;
  if (c_status == '1' && p_status == '0')
    ret = AUDIO_STATE_CAPTURE;
  if (c_status == '0' && p_status == '1')
    ret = AUDIO_STATE_PLAYBACK;
  return ret;
}

static void uevent_event(UeventData *payload, EventCallback uevent_cb,
                         UmdEventCallback umd_event_cb) {
  char msg[UEVENT_MSG_LEN + 2] = {0};
  int n;
  char event[] = UEVENT_MSG;
  n = uevent_kernel_multicast_recv(payload->fd, msg, UEVENT_MSG_LEN);
  if (n <= 0 || n >= UEVENT_MSG_LEN) return;
  if (!strncmp(msg, event, sizeof(event))) {
    AudioState newstate = get_audio_client_status();
    if (payload->cur != newstate) {
      payload->cur = newstate;
      umd_event_cb(payload->cur, uevent_cb);
      UMD_LOG_INFO("\nSending audio uevent with state %d\n", payload->cur);
    }
  }
}

int32_t UmdUtil::monitor_audio_status(EventCallback uevent_cb,
                                      UmdEventCallback umd_event_cb) {
  mActive = true;
  int uevent_fd, epoll_fd, nevents, n;
  struct epoll_event ev;
  struct epoll_event events[64];
  UeventData payload;

  uevent_fd = uevent_open_socket(64 * 1024, true);
  if (uevent_fd < 0) {
    UMD_LOG_ERROR("\nuevent open socket failed\n");
    return -1;
  }

  fcntl(uevent_fd, F_SETFL, O_NONBLOCK);

  ev.events = EPOLLIN;
  ev.data.ptr = (void *)uevent_event;

  epoll_fd = epoll_create(64);
  if (epoll_fd == -1) {
    UMD_LOG_ERROR("\nepoll_create failed\n");
    close(uevent_fd);
    return -1;
  }

  payload.fd = uevent_fd;
  payload.cur = AUDIO_STATE_INVALID;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, uevent_fd, &ev) == -1) {
    UMD_LOG_ERROR("\nepoll_ctl failed\n");
    close(uevent_fd);
    close(epoll_fd);
    return -1;
  }

  while (mActive) {
    nevents = epoll_wait(epoll_fd, events, 64, -1);

    if (nevents == -1) {
      if (errno == EINTR) continue;
      break;
    }
    for (n = 0; n < nevents; ++n) {
      if (events[n].data.ptr)
        (*(void (*)(UeventData * payload, EventCallback uevent_cb,
                    UmdEventCallback umd_event_cb)) events[n]
              .data.ptr)(
            &payload, uevent_cb, umd_event_cb);
    }
  }

  close(uevent_fd);
  close(epoll_fd);
  return 0;
}