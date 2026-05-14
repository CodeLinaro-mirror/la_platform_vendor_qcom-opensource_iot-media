LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_BOARD_PLATFORM), sun)
USE_AIDL_ALLOCATOR := true
endif

include $(CLEAR_VARS)

LOCAL_CPP_EXTENSION := .cc

LOCAL_SRC_FILES := camera_memory_interface.cc

ifdef USE_AIDL_ALLOCATOR
LOCAL_SRC_FILES += allocator_aidl_interface.cc
else
LOCAL_SRC_FILES += allocator_hidl_interface.cc
endif

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/..

LOCAL_SHARED_LIBRARIES := \
    libcamera_utils \
    libcamera_metadata \
    libhidlbase \
    libutils \
    libcutils \
    liblog \
    libbinder_ndk \
    android.hardware.camera.metadata-V1-ndk \
    android.hardware.camera.provider-V1-ndk \
    android.hardware.camera.device-V1-ndk \
    android.hardware.camera.common-V1-ndk

ifdef USE_AIDL_ALLOCATOR
LOCAL_CFLAGS += -DUSE_AIDL_ALLOCATOR
LOCAL_SHARED_LIBRARIES += \
    libui \
    android.hardware.graphics.common-V5-ndk
else
LOCAL_SHARED_LIBRARIES += \
    libhwbinder \
    android.hardware.graphics.allocator@4.0 \
    android.hardware.graphics.mapper@4.0 \
    android.hardware.graphics.common@1.0 \
    libgralloctypes
endif

LOCAL_STATIC_LIBRARIES := \
    android.hardware.camera.common@1.0-helper \
    libaidlcommonsupport

LOCAL_MODULE = libcamera_memory_interface
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)
