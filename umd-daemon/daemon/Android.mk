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

LOCAL_SRC_FILES := umd-camera.cpp \
                   umd-fake-camera.cpp \
                   audio-stream.cpp \
                   audio-pcm.cpp \
                   umd.cpp \
                   umd-audio.cpp \
                   umd-util.cpp  \
                   umd-video-data-processing.cpp

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

LOCAL_SHARED_LIBRARIES := \
    libqtiumd \
    libcamera_adaptor \
    libcamera_memory_interface \
    libcamera_utils \
    libcamera_metadata \
    libtinyalsa \
    liblog \
    libbase \
    libutils \
    libcutils \
    libfmq \
    libhardware \
    android.hardware.camera.provider@2.4 \
    android.hardware.camera.device@1.0 \
    android.hardware.camera.device@3.2 \
    android.hardware.camera.metadata@3.4 \
    android.hardware.camera.common@1.0 \
    android.hardware.graphics.common@1.0 \
    android.hardware.graphics.common@1.0 \
    android.hardware.graphics.bufferqueue@2.0 \
    vendor.qti.hardware.umd@1.0

ifeq ($(call is-board-platform-in-list, bengal),true)
LOCAL_SHARED_LIBRARIES += android.hardware.graphics.allocator@4.0
LOCAL_SHARED_LIBRARIES += android.hardware.graphics.mapper@4.0
LOCAL_CPPFLAGS += -DALLOCATOR_IMAPPER_V4
else
LOCAL_SHARED_LIBRARIES += android.hardware.graphics.allocator@3.0
LOCAL_SHARED_LIBRARIES += android.hardware.graphics.mapper@3.0
endif

LOCAL_STATIC_LIBRARIES := \
    android.hardware.camera.common@1.0-helper

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
