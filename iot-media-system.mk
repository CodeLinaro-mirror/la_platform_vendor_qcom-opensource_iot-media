# umd-daemon application to access camera and audio frames
# and pass it on to UMD-Gadget module

PRODUCT_PACKAGES += libumd-adaptor

LEGACY_HIDL_LEVELS := 26 27 28 29 30 31 32 33

ifneq ($(filter $(BOARD_SHIPPING_API_LEVEL),$(LEGACY_HIDL_LEVELS)),)
PRODUCT_PACKAGES += vendor.qti.hardware.umd@1.0-service
else
PRODUCT_PACKAGES += vendor.qti.hardware.umd-audio-service
PRODUCT_PACKAGES += vendor.qti.hardware.umd-camera-service
endif

# AI Director test & SNPE Lib
# AI_DIRECTOR := true
ifeq ($(AI_DIRECTOR), true)
PRODUCT_PACKAGES += ai_director_test
PRODUCT_PACKAGES += libSNPE
endif

# SNPE wrapper library
PRODUCT_PACKAGES += libai_snpe_wrapper
