LOCAL_PATH := $(call my-dir)
SHARED := $(LOCAL_PATH)/../../shared

include $(CLEAR_VARS)
# Library name kept as "runet": MainActivity loads it with System.loadLibrary("runet").
LOCAL_MODULE := runet
LOCAL_C_INCLUDES := $(SHARED)/include
LOCAL_SRC_FILES := main.cpp android_platform.cpp hook.cpp \
                   ../../shared/src/platform.cpp ../../shared/src/win10_ui.cpp \
                   ../../shared/src/keyboard_mouse.cpp
LOCAL_LDLIBS := -llog -ldl
include $(BUILD_SHARED_LIBRARY)
