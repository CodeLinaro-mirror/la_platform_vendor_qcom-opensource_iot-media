LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE = libumd-adaptor
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(TARGET_BOARD_PLATFORM),kona)
LOCAL_CFLAGS += -DTARGET_KONA
endif

ifeq ($(TARGET_BOARD_PLATFORM),lahaina)
LOCAL_CFLAGS += -DTARGET_LAHAINA
endif

LOCAL_SRC_FILES := umd-camera.cpp \
                   audio-stream.cpp \
                   audio-pcm.cpp \
                   umd.cpp \
                   umd-audio.cpp \
                   umd-util.cpp

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
    android.hardware.graphics.allocator@3.0 \
    android.hardware.graphics.mapper@3.0 \
    android.hardware.graphics.common@1.0 \
    vendor.qti.hardware.umd@1.0

LOCAL_STATIC_LIBRARIES := \
    android.hardware.camera.common@1.0-helper

LOCAL_HEADER_LIBRARIES := libqtiumdheaders

include $(BUILD_SHARED_LIBRARY)
