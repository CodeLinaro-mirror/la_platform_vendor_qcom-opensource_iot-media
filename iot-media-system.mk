# umd-daemon application to access camera and audio frames
# and pass it on to UMD-Gadget module
PRODUCT_PACKAGES += umd-daemon

ifeq ($(TARGET_BOARD_PLATFORM),kona)
ifeq ($(TARGET_KERNEL_VERSION), 4.19)
# AI Director test & SNPE Lib
# AI_DIRECTOR := true
ifeq ($(AI_DIRECTOR), true)
PRODUCT_PACKAGES += ai_director_test
PRODUCT_PACKAGES += libSNPE
endif

# SNPE wrapper library
PRODUCT_PACKAGES += libai_snpe_wrapper
endif
endif
