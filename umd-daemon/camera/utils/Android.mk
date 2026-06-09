LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CPP_EXTENSION := .cc

ifneq (,$(filter $(TARGET_BOARD_PLATFORM), sun lahaina lahaina612))
USE_AIDL_ALLOCATOR := true
endif

ifdef USE_AIDL_ALLOCATOR
LOCAL_CFLAGS += -DUSE_AIDL_ALLOCATOR
endif

LOCAL_SRC_FILES := camera_condition.cc
LOCAL_SRC_FILES += camera_thread.cc

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

LOCAL_SHARED_LIBRARIES := \
    libutils \
    liblog \
    libbinder_ndk

LOCAL_STATIC_LIBRARIES := libaidlcommonsupport

LOCAL_CPPFLAGS += -fexceptions

LOCAL_MODULE = libcamera_utils
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)
