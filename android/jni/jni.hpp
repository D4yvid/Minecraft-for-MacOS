#ifndef JNI_H
#define JNI_H

#include "jni.h"
#include <cstdint>

namespace runet
{
namespace jni
{
	class JavaEnviroment
	{
	private:
		JavaVM *vm;
		JNIEnv *env;

		jclass VMRuntimeClass;
		jobject VMRuntimeInstance;
		jmethodID VMRuntime_setTargetSdkVersion;

	public:
		JavaEnviroment(JavaVM *vm);

		void SetCurrentJavaEnviroment();

		void DalvikSetTargetSdkVersion(uint32_t newSdk);

		static JavaEnviroment *GetInstance();
	};

	void android_application_set_target_sdk_version(uint32_t sdkVersion);
}; // namespace jni;
}; // namespace runet;

#endif /** JNI_H */
