LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE = libcamera_adaptor
LOCAL_PROPRIETARY_MODULE := true

LOCAL_CPPFLAGS += -fexceptions
LOCAL_CPPFLAGS += -DQCAMERA3_TAG_LOCAL_COPY

LEGACY_HIDL_LEVELS := 26 27 28 29 30 31 32 33

ifeq ($(filter $(BOARD_SHIPPING_API_LEVEL),$(LEGACY_HIDL_LEVELS)),)
LOCAL_CPPFLAGS += -DUSE_AIDL_DESC
endif

LOCAL_CPP_EXTENSION := .cc

LOCAL_SRC_FILES := camera_device_client.cc
LOCAL_SRC_FILES += camera_monitor.cc
LOCAL_SRC_FILES += camera_request_handler.cc
LOCAL_SRC_FILES += camera_prepare_handler.cc
LOCAL_SRC_FILES += camera_stream.cc
LOCAL_SRC_FILES += camera_utils.cc
LOCAL_SRC_FILES += camera_hidl_vendor_tag_descriptor.cc

LOCAL_C_INCLUDES += $(LOCAL_PATH)/..
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../include

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDE_DIRS += $(LOCAL_PATH)/..
LOCAL_EXPORT_C_INCLUDE_DIRS += $(LOCAL_PATH)/../include

LOCAL_SHARED_LIBRARIES := \
    libhidlbase \
    libbinder_ndk \
    libhwbinder \
    libui \
    libbinder \
    libutils \
    libcutils \
    libfmq \
    liblog \
    libc++ \
    libhidlmemory \
    libgralloctypes \
    libcamera_metadata \
    android.hardware.camera.provider-V1-ndk \
    android.hardware.camera.common@1.0 \
    android.hardware.camera.device-V1-ndk \
    android.hardware.camera.metadata-V1-ndk \
    android.hardware.graphics.common@1.0 \
    android.hardware.graphics.allocator@4.0 \
    android.hardware.graphics.mapper@4.0 \
    libcamera_utils \
    libcamera_memory_interface

LOCAL_STATIC_LIBRARIES := \
    android.hardware.camera.common@1.0-helper \
    libaidlcommonsupport \
    android.hardware.camera.common-V1-ndk \
    android.hardware.common-V2-ndk \
    android.hardware.common.fmq-V1-ndk \
    android.hardware.graphics.common-V3-ndk \
    libgrallocusage

include $(BUILD_SHARED_LIBRARY)
