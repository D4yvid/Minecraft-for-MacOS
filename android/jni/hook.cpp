#include "hook.hpp"

#include <dlfcn.h>

#include "log.hpp"
#include "vtable_scan.hpp"

using namespace runet::hook;

VirtualTable::VirtualTable(hook::soinfo *handle, std::string symbolName)
	: handle(handle)
{
	this->data = (void **)::dlsym(handle, symbolName.c_str());
}

void VirtualTable::Hook(int index, void *replacement, void **original)
{
	if (!this->data || index < 2)
		return;

	if (original)
		*original = this->data[index];

	this->data[index] = replacement;
}

int VirtualTable::FindIndex(std::string symbolName)
{
	void *needle = ::dlsym(this->handle, symbolName.c_str());
	int index = mcfm::android::find_vtable_index(this->data, needle);

	if (index < 0)
		LOGE("vtable: couldn't find %s in vtable!", symbolName.c_str());
	else
		LOGI("%s: %p (index: %i)", symbolName.c_str(), needle, index);

	return index;
}
