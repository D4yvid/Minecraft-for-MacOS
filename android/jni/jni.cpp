#include "jni.h"
#include "jni.hpp"
#include "log.hpp"
#include "android.hpp"
#include <sys/stat.h>
#include <cstdlib>

using namespace runet::jni;

static JavaEnviroment *currentJavaEnviroment;

JavaEnviroment::JavaEnviroment(JavaVM *vm)
{
	this->vm = vm;

	LOGI("Using JVM: %p", this->vm);

	this->vm->GetEnv((void **) &this->env, JNI_VERSION_1_2);

	LOGI("Finding class 'dalvik.system.VMRuntime'");

	this->VMRuntimeClass = this->env->FindClass("dalvik/system/VMRuntime");

	if (!this->VMRuntimeClass) {
		LOGE("Couldn't find dalvik.system.VMRuntime class");

		return;
	}

	LOGI("Found! VMRuntime is at %p", this->VMRuntimeClass);

	jmethodID getRuntimeId = this->env->GetStaticMethodID(this->VMRuntimeClass, "getRuntime", "()Ldalvik/system/VMRuntime;");

	if (!getRuntimeId)
	{
		LOGE("Cannot find getRuntime in dalvik/system/VMRuntime!");

		return;
	}

	this->VMRuntimeInstance = this->env->CallStaticObjectMethod(this->VMRuntimeClass, getRuntimeId);

	if (!this->VMRuntimeInstance)
	{
		LOGE("Cannot get VMRuntime instance!");

		return;
	}

	LOGI("VMRuntimeInstance: %p", VMRuntimeInstance);

	this->VMRuntime_setTargetSdkVersion = this->env->GetMethodID(this->VMRuntimeClass, "setTargetSdkVersion", "(I)V");

	if (this->VMRuntime_setTargetSdkVersion == nullptr) {
		LOGE("Cannot get setTargetSdkVersion!");

		return;
	}

	LOGI("setTargetSdkVersionId: %p", this->VMRuntime_setTargetSdkVersion);
}

void JavaEnviroment::SetCurrentJavaEnviroment()
{
	currentJavaEnviroment = this;
}

void JavaEnviroment::DalvikSetTargetSdkVersion(uint32_t newSdk)
{
	JNIEnv *env;

 	LOGI("this: %p", this);
 	LOGI("this->vm: %p", this->vm);
	int status = this->vm->GetEnv((void **)&env, JNI_VERSION_1_2);
	LOGI("jnienv: %p", env);

	if (status == JNI_EDETACHED) {
		LOGI("attaching");
		this->vm->AttachCurrentThread(&env, nullptr);
	}

	LOGI("calling method");
	env->CallVoidMethod(this->VMRuntimeInstance, this->VMRuntime_setTargetSdkVersion, (jint) newSdk);

	LOGI("Current target sdk: %d", runet::android::android_get_application_target_sdk_version());

	if (status == JNI_EDETACHED) {
		LOGI("detaching");
		this->vm->DetachCurrentThread();
	}
}

JavaEnviroment *JavaEnviroment::GetInstance()
{
    return currentJavaEnviroment;
}

void runet::jni::android_application_set_target_sdk_version(uint32_t sdkVersion)
{
	JavaEnviroment::GetInstance()->DalvikSetTargetSdkVersion(sdkVersion);
}
