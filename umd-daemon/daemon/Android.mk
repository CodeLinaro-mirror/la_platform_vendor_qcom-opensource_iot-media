LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE = libumd-adaptor
LOCAL_PROPRIETARY_MODULE := true
LOCAL_CFLAGS += -fcxx-exceptions

ifeq ($(TARGET_BOARD_PLATFORM),kona)
LOCAL_CFLAGS += -DTARGET_KONA
endif

ifeq ($(TARGET_BOARD_PLATFORM),lahaina)
LOCAL_CFLAGS += -DTARGET_LAHAINA
endif

LEGACY_HIDL_LEVELS := 26 27 28 29 30 31 32 33

ifeq ($(filter $(BOARD_SHIPPING_API_LEVEL),$(LEGACY_HIDL_LEVELS)),)
LOCAL_CPPFLAGS += -DUSE_PCM_WRITE
endif

LOCAL_SRC_FILES := umd-camera.cpp \
                   umd-fake-camera.cpp \
                   audio-stream.cpp \
                   audio-pcm.cpp \
                   umd.cpp \
                   umd-audio.cpp \
                   umd-util.cpp \
                   umd-video-data-processing.cpp

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

LOCAL_SHARED_LIBRARIES := \
    libbinder_ndk \
    libcamera_metadata \
    libtinyalsa \
    liblog \
    libbase \
    libutils \
    libcutils \
    libfmq \
    libhardware \
    android.hardware.camera.common@1.0 \
    android.hardware.camera.device-V1-ndk \
    android.hardware.camera.metadata-V1-ndk \
    android.hardware.camera.provider-V1-ndk \
    android.hardware.camera.common-V1-ndk \
    android.hardware.graphics.common@1.0 \
    android.hardware.graphics.allocator@4.0 \
    android.hardware.graphics.mapper@4.0 \
    android.hardware.graphics.common@1.0 \
    android.hardware.graphics.bufferqueue@2.0 \
    vendor.qti.hardware.umd@1.0 \
    libqtiumd \
    libcamera_adaptor \
    libcamera_memory_interface \
    libcamera_utils

LOCAL_STATIC_LIBRARIES := \
    android.hardware.camera.common@1.0-helper \
    libaidlcommonsupport

LOCAL_HEADER_LIBRARIES := libqtiumdheaders


ifneq ($(TARGET_KERNEL_VERSION), 4.19)
LOCAL_CFLAGS += -DENABLE_H264

LOCAL_SHARED_LIBRARIES += \
    libqcodec2_core \
    libcodec2_vndk \
    libcodec2 \
    libqtic2module

LOCAL_HEADER_LIBRARIES += libqcodec2_core_headers \
                          libqcodec2_base_headers \
                          libqcodec2_basecodec_headers \
                          libcodec2_internal \
                          display_intf_headers \
                          libqcodec2_core_api_headers \
                          libqtic2headers
endif

include $(BUILD_SHARED_LIBRARY)
