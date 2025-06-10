ifeq ($(call is-board-platform-in-list,kona lahaina kalama),true)

IOT_MEDIA_PATH:= $(call my-dir)

LEGACY_HIDL_LEVELS := 26 27 28 29 30 31 32 33

include $(IOT_MEDIA_PATH)/umd-daemon/camera/Android.mk
include $(IOT_MEDIA_PATH)/umd-daemon/daemon/Android.mk

ifneq ($(filter $(BOARD_SHIPPING_API_LEVEL),$(LEGACY_HIDL_LEVELS)),)
include $(IOT_MEDIA_PATH)/umd-daemon/hidl-impl/Android.mk
else
include $(IOT_MEDIA_PATH)/umd-daemon/aidl-impl/Android.mk
endif

# Enable AI Director test
# AI_DIRECTOR := true
ifeq ($(AI_DIRECTOR),true)
include $(IOT_MEDIA_PATH)/ai-director-test/Android.mk
endif
include $(IOT_MEDIA_PATH)/snpe_wrapper/Android.mk

endif
