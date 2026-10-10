LOCAL_PATH := $(call my-dir)
SHARED := $(LOCAL_PATH)/../../shared

include $(CLEAR_VARS)
# MainActivity loads it with System.loadLibrary("mcfm") (android/tools/patch_smali.py).
LOCAL_MODULE := mcfm
LOCAL_C_INCLUDES := $(SHARED)/include
LOCAL_SRC_FILES := main.cpp android_platform.cpp hook.cpp \
                   ../../shared/src/platform.cpp ../../shared/src/win10_ui.cpp \
                   ../../shared/src/keyboard_mouse.cpp
LOCAL_LDLIBS := -llog -ldl
include $(BUILD_SHARED_LIBRARY)
