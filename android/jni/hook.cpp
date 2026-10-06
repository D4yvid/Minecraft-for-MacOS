#include "hook.hpp"

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "log.hpp"
#include "vtable_scan.hpp"

using namespace runet::hook;

VirtualTable::VirtualTable(hook::soinfo *handle, std::string symbolName)
	: handle(handle)
{
	this->data = (void **)::dlsym(handle, symbolName.c_str());
}

bool VirtualTable::Hook(int index, void *replacement, void **original)
{
	if (!this->data || index < 2)
		return false;

	// vtables live in .data.rel.ro (read-only after relocation): unprotect just this page.
	uintptr_t pageSize = (uintptr_t)sysconf(_SC_PAGESIZE);
	uintptr_t page = (uintptr_t)&this->data[index] & ~(pageSize - 1);
	if (mprotect((void *)page, pageSize, PROT_READ | PROT_WRITE) != 0)
	{
		LOGE("vtable: mprotect failed: %s", strerror(errno));
		return false;
	}

	if (original)
		*original = this->data[index];

	this->data[index] = replacement;
	return true;
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
