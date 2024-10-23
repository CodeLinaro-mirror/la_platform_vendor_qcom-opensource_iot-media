LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CPP_EXTENSION := .cc

LOCAL_SRC_FILES := camera_memory_interface.cc
LOCAL_SRC_FILES += allocator_hidl_interface.cc

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/..

LOCAL_SHARED_LIBRARIES := \
    libcamera_utils \
    libcamera_metadata \
    libhidlbase \
    libhwbinder \
    libutils \
    libcutils \
    liblog \
    libbinder_ndk \
    android.hardware.graphics.allocator@4.0 \
    android.hardware.graphics.mapper@4.0 \
    android.hardware.camera.metadata-V1-ndk \
    android.hardware.camera.provider-V1-ndk \
    android.hardware.camera.device-V1-ndk \
    android.hardware.camera.common-V1-ndk \
    android.hardware.graphics.common@1.0 \
    libgralloctypes

LOCAL_STATIC_LIBRARIES := \
    android.hardware.camera.common@1.0-helper \
    libaidlcommonsupport

LOCAL_MODULE = libcamera_memory_interface
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)