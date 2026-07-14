ifeq ($(call is-board-platform-in-list,kona lahaina bengal),true)

IOT_MEDIA_PATH:= $(call my-dir)

include $(IOT_MEDIA_PATH)/umd-daemon/camera/Android.mk
include $(IOT_MEDIA_PATH)/umd-daemon/daemon/Android.mk
include $(IOT_MEDIA_PATH)/umd-daemon/hidl-impl/Android.mk

# Enable AI Director test
# AI_DIRECTOR := true
ifeq ($(AI_DIRECTOR),true)
include $(IOT_MEDIA_PATH)/ai-director-test/Android.mk
endif
include $(IOT_MEDIA_PATH)/snpe_wrapper/Android.mk

endif
