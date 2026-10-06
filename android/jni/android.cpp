#include "android.hpp"
#include "log.hpp"
#include "hook.hpp"
#include "jni.hpp"

void (*_android_set_application_target_sdk_version)(uint32_t);
uint32_t (*_android_get_application_target_sdk_version)();

bool runet::android::InitializeFunctions()
{
    auto lLibcHandle = (runet::hook::soinfo *) ::dlopen("libc.so", RTLD_LAZY | RTLD_LOCAL);

    if (lLibcHandle == nullptr)
    {
        LOGE("cannot open libc.so: %s", dlerror());

        return false;
    }

    _android_set_application_target_sdk_version = (void (*)(uint32_t))dlsym(
        lLibcHandle,
        "android_set_application_target_sdk_version");

    _android_get_application_target_sdk_version = (uint32_t(*)())dlsym(
        lLibcHandle,
        "android_get_application_target_sdk_version");

    if (!_android_get_application_target_sdk_version)
    {
        LOGE("couldn't find android_get_application_target_sdk_version");

        return false;
    }

    if (!_android_set_application_target_sdk_version)
    {
        // TODO: create a JNI wrapper using 'dalvik.system.VMRuntime' to create a set_target_sdk_version
        LOGE("android_set_application_target_sdk_version not found, using JNI wrapper...");

        _android_set_application_target_sdk_version = [](uint32_t sdkVersion) { runet::jni::JavaEnviroment::GetInstance()->DalvikSetTargetSdkVersion(sdkVersion); };
    }

    return true;
}

void runet::android::android_set_application_target_sdk_version(uint32_t sdkVersion)
{
    _android_set_application_target_sdk_version(sdkVersion);
}

uint32_t runet::android::android_get_application_target_sdk_version()
{
    return _android_get_application_target_sdk_version();
}