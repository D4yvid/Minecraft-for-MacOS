#ifndef ANDROID_HPP
#define ANDROID_HPP

#include <cstdint>

namespace runet
{
namespace android
{

    void android_set_application_target_sdk_version(uint32_t version);
    uint32_t android_get_application_target_sdk_version();

    bool InitializeFunctions();

}
}

#endif /** ANDROID_HPP */