# umd-daemon application to access camera and audio frames
# and pass it on to UMD-Gadget module
ifneq ($(TARGET_HAS_QTI_OPTIMIZATIONS), true)
PRODUCT_PACKAGES += libumd-adaptor
PRODUCT_PACKAGES += vendor.qti.hardware.umd@1.0-service
endif #TARGET_HAS_QTI_OPTIMIZATIONS

# AI Director test & SNPE Lib
# AI_DIRECTOR := true
ifeq ($(AI_DIRECTOR), true)
PRODUCT_PACKAGES += ai_director_test
PRODUCT_PACKAGES += libSNPE
endif

# SNPE wrapper library
PRODUCT_PACKAGES += libai_snpe_wrapper
