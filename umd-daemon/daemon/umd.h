/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once
#include <string>

#include "umd-util.h"

void init_uvc();
int32_t start_uvc();
void stop_uvc();
void deinit_uvc();
int32_t get_gadget_cnt();

int32_t init_uac(AudioCallback cb);
void deinit_uac();

int32_t submit_buffer(uint8_t* data);
void set_buffersize(size_t bufSize);
int32_t uevent_monitor(EventCallback uevent_cb);