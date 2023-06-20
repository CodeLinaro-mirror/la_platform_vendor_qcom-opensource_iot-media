LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := vendor.qti.hardware.umd@1.0-service
LOCAL_INIT_RC := vendor.qti.hardware.umd@1.0-service.rc
LOCAL_VINTF_FRAGMENTS := vendor.qti.hardware.umd@1.0-service.xml
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := \
    UMDAdaptor.cpp \
    service.cpp

LOCAL_SHARED_LIBRARIES := \
    libhidlbase \
    libhidltransport \
    libutils \
    liblog \
    libcutils \
    libhardware \
    libbase \
    libhwbinder \
    vendor.qti.hardware.umd@1.0 \
    libumd-adaptor

include $(BUILD_EXECUTABLE)
