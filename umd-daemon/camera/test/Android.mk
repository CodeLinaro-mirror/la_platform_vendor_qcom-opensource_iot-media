LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE = camera_test
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SRC_FILES := camera_test.cpp

LOCAL_SHARED_LIBRARIES := \
    libcamera_adaptor \
    libcamera_memory_interface \
    libcamera_metadata \
    libcamera_utils \
    libutils \
    libcutils \
    liblog \
    libfmq \
    libbinder_ndk \
    android.hardware.graphics.allocator@4.0 \
    android.hardware.graphics.mapper@4.0 \
    android.hardware.camera.metadata-V1-ndk \
    android.hardware.camera.provider-V1-ndk \
    android.hardware.camera.device-V1-ndk \
    android.hardware.camera.common-V1-ndk \
    android.hardware.graphics.common@1.0

LOCAL_STATIC_LIBRARIES := \
    android.hardware.camera.common@1.0-helper \
    android.hardware.camera.common-V1-ndk \
    libaidlcommonsupport

#include $(BUILD_EXECUTABLE)