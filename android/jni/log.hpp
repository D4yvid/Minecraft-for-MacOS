#pragma once

#include <android/log.h>

#if RELEASE_BUILD > 0
#define LOGI(...)
#define LOGW(...)
#define LOGD(...)
#define LOGE(...)
#else
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "mcfm", (char *) __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "mcfm", (char *) __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "mcfm", (char *) __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "mcfm", (char *) __VA_ARGS__)
#endif