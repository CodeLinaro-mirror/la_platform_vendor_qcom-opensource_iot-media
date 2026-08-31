LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := vendor.qti.hardware.umd_aidl-service
LOCAL_VINTF_FRAGMENTS := vendor.qti.hardware.umd_aidl.xml
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := \
    UMDAdaptor.cpp

LOCAL_SHARED_LIBRARIES := \
    libbase \
    liblog \
    libhardware \
    libbinder_ndk \
    libbinder \
    libutils \
    vendor.qti.hardware.umd_aidl-V1-ndk \
    libumd-adaptor

include $(BUILD_SHARED_LIBRARY)

# ==========================================================
# AUDIO process executable
# ==========================================================
include $(CLEAR_VARS)
LOCAL_MODULE := vendor.qti.hardware.umd-audio-service
LOCAL_INIT_RC := vendor.qti.hardware.umd-audio-service.rc
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SRC_FILES := audio_service.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    liblog \
    libcutils \
    libhardware \
    libbase \
    libbinder_ndk \
    libbinder \
    libumd-adaptor \

# Init file for audio process
LOCAL_SHARED_LIBRARIES += vendor.qti.hardware.umd_aidl-V1-ndk \
                          vendor.qti.hardware.umd_aidl-service

LOCAL_CFLAGS += -Wall -Wextra -Werror
include $(BUILD_EXECUTABLE)

# ==========================================================
# CAMERA process executable
# ==========================================================
include $(CLEAR_VARS)
LOCAL_MODULE := vendor.qti.hardware.umd-camera-service
LOCAL_INIT_RC := vendor.qti.hardware.umd-camera-service.rc
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SRC_FILES := camera_service.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    liblog \
    libcutils \
    libhardware \
    libbase \
    libbinder_ndk \
    libbinder \
    libumd-adaptor \

LOCAL_SHARED_LIBRARIES += vendor.qti.hardware.umd_aidl-V1-ndk \
                          vendor.qti.hardware.umd_aidl-service

LOCAL_CFLAGS += -Wall -Wextra -Werror
include $(BUILD_EXECUTABLE)
