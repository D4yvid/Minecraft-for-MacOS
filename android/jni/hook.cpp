#include "hook.hpp"
#include <sys/sysconf.h>
#include "jni.hpp"
#include "log.hpp"
#include <android/log.h>
#include <cstdint>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/mman.h>	
#include "android.hpp"

using namespace runet::hook;

/// @brief Current application target sdk version
uint32_t lApplicationTarget;

/// @brief The bionic libc handle
soinfo *lLibcHandle;

/// @brief True if base hooking is initialized properly
bool lInitializedBaseHooking;

bool runet::hook::InitializeBaseHooking()
{
	lApplicationTarget = runet::android::android_get_application_target_sdk_version();

	LOGI("Application target sdk version: %d", lApplicationTarget);

	lInitializedBaseHooking = true;

	return true;
}

/// @brief A wrapper for dlopen(const char *, int) using some tweaks
soinfo *runet::hook::LoadLibrary(std::string pFilename, int pFlags)
{
	if (!lInitializedBaseHooking)
	{
		LOGE("Hooking base was not initialized, refusing to dlopen");

		return nullptr;
	}

	soinfo *lHandle = (soinfo *)::dlopen(pFilename.c_str(), pFlags);

	LOGI("dlopen(\"%s\", %d) = %p", pFilename.c_str(), pFlags, lHandle);

	if (((size_t)lHandle & 1) != 0)
	{
		LOGI("Your system is giving a randomly generated handle for dlopen(), trying to bypass");

		// "Android Nougat and above uses a randomly generated handle for dlopen."
		//  - https://github.com/zhuowei/MCPELauncher/blob/75dccc348ef7a136e916a8231f266c62fb71ca96/jni/prepatch.cpp#L306
		//
		// We'll need to set our application target to below Android Nougat (Marshmallow), to get a normal handle (if we're lucky)

		runet::android::android_set_application_target_sdk_version(ANDROID_MARSHMALLOW_MR1_TARGET);

		if (runet::android::android_get_application_target_sdk_version() != ANDROID_MARSHMALLOW_MR1_TARGET)
		{
			// Couldn't set application target, refuse to do anything
			LOGE("Couldn't bypass application target to try and get random handle, returning NULL");

			return nullptr;
		}

		soinfo *lNewHandle = (soinfo *)::dlopen(pFilename.c_str(), pFlags);

		runet::android::android_set_application_target_sdk_version(lApplicationTarget);

		if (runet::android::android_get_application_target_sdk_version() != lApplicationTarget)
		{
			LOGE("Couldn't restore default application target, exiting.");

			abort();
		}

		if (!lNewHandle || ((size_t)lNewHandle & 1) != 0)
		{
			LOGE("Even after setting application target to marshmallow, we got a random handle. returning NULL");

			return nullptr;
		}

		LOGI("Successfuly got a not-random handle, the old (random) handle is %p and the new is %p", lHandle, lNewHandle);

		return lNewHandle;
	}

	return lHandle;
}

// Hooker::Hooker(hook::soinfo *pLibraryHandle, std::initializer_list<Hook> pHooks)
// 	: mLibraryHandle(pLibraryHandle)
// 	, mHooks(std::vector<Hook>(pHooks)) {}

// bool Hooker::Apply()
// {
// 	for (auto &lHook : this->mHooks)
// 	{
// 		std::string lSymbolName = lHook.symbolName;
// 		FunctionHook lHookInfo = lHook.hook;

// 		auto lSymbol = ::dlsym(this->mLibraryHandle, lSymbolName.c_str());

// 		if (lSymbol == nullptr)
// 		{
// 			LOGW("Hooker::Apply(at %s): The symbol doesn't exist on the specified library. Not applying hook", lSymbolName.c_str());

// 			continue;
// 		}

// 		LOGI("Hooker::Apply(at %s): Applying hook...", lSymbolName.c_str());

// 		MSHookFunction((void *)lSymbol, (void *)lHookInfo.function, (void **)&lHookInfo.originalFunction);

// 		lHookInfo.hooked = true;

// 		LOGI("Hooker::Apply(at %s): Done, hook applied", lSymbolName.c_str());
// 	}

// 	return true;
// }

// bool Hooker::Restore()
// {
// 	for (auto &lHook : this->mHooks)
// 	{
// 		auto &lSymbolName = lHook.symbolName;
// 		auto &lHookInfo = lHook.hook;

// 		LOGI("Hooker::Restore: Restoring %s", lSymbolName.c_str());

// 		auto lSymbol = ::dlsym(this->mLibraryHandle, lSymbolName.c_str());

// 		if (lSymbol == nullptr)
// 		{
// 			LOGW("Hooker::Restore(at %s): The symbol doesn't exist on the specified library. Not restoring hook", lSymbolName.c_str());

// 			continue;
// 		}

// 		LOGI("Hooker::Restore(at %s): Restoring original function hook...", lSymbolName.c_str());

// 		MSHookFunction((void *)lSymbol, (void *)lHookInfo.originalFunction, nullptr);

// 		lHookInfo.hooked = false;

// 		LOGI("Hooker::Restore(at %s): Done, hook removed", lSymbolName.c_str());
// 	}

// 	return true;
// }

VirtualTable::VirtualTable(hook::soinfo *handle, std::string symbolName)
	: handle(handle)
{
	this->data = (void **)::dlsym(handle, symbolName.c_str());
}

void VirtualTable::Hook(int index, void *replacement, void **original)
{
	if (original)
		*original = this->data[index];

	this->data[index] = replacement;
}

void VirtualTable::Hook(int index, void *replacement)
{
	this->Hook(index, replacement, nullptr);
}

void *VirtualTable::GetFunctionAtIndex(int index)
{
	return this->data[index];
}

int VirtualTable::FindIndex(std::string symbolName)
{
	void *needle = ::dlsym(this->handle, symbolName.c_str());

	for (int i = 0; i < 512; i++)
	{
		if (this->data[i] == needle)
		{
			LOGI("%s: %p (index: %i)", symbolName.c_str(), this->data[i], i);
			return i;
		}
	}

	LOGE("vtable: couldn't find %s in vtable!", symbolName.c_str());

	return -1;
}