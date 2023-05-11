ifeq ($(call is-board-platform-in-list,kona lahaina),true)

IOT_MEDIA_PATH:= $(call my-dir)

ifeq ($(TARGET_BOARD_PLATFORM),kona)
include $(IOT_MEDIA_PATH)/umd-daemon/camera/Android.mk
include $(IOT_MEDIA_PATH)/umd-daemon/daemon/Android.mk
include $(IOT_MEDIA_PATH)/umd-daemon/hidl-impl/Android.mk
ifeq ($(TARGET_KERNEL_VERSION), 4.19)
# Enable AI Director test
# AI_DIRECTOR := true
ifeq ($(AI_DIRECTOR),true)
include $(IOT_MEDIA_PATH)/ai-director-test/Android.mk
endif
include $(IOT_MEDIA_PATH)/snpe_wrapper/Android.mk
endif
endif

endif
