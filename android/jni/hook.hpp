#pragma once

#include <linux/elf.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "log.hpp"
#include <dlfcn.h>
#include <map>

namespace mcfm
{
namespace hook
{

	/*============================ START ANDROID CODE ============================*/
	/*
	 * Copyright (C) 2008 The Android Open Source Project
	 * All rights reserved.
	 *
	 * Redistribution and use in source and binary forms, with or without
	 * modification, are permitted provided that the following conditions
	 * are met:
	 *  * Redistributions of source code must retain the above copyright
	 *    notice, this list of conditions and the following disclaimer.
	 *  * Redistributions in binary form must reproduce the above copyright
	 *    notice, this list of conditions and the following disclaimer in
	 *    the documentation and/or other materials provided with the
	 *    distribution.
	 *
	 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
	 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
	 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
	 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
	 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
	 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
	 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
	 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
	 * SUCH DAMAGE.
	 */

	/* magic shared structures that GDB knows about */
	struct link_map
	{
			uintptr_t l_addr;
			char * l_name;
			uintptr_t l_ld;
			struct link_map * l_next;
			struct link_map * l_prev;
	};


	typedef struct soinfo soinfo;

#define FLAG_LINKED     0x00000001
#define FLAG_ERROR      0x00000002
#define FLAG_EXE        0x00000004 // The main executable
#define FLAG_LINKER     0x00000010 // The linker itself

#define SOINFO_NAME_LEN 128

	struct soinfo
	{
			const char name[SOINFO_NAME_LEN];
			Elf32_Phdr *phdr;
			int phnum;
			unsigned entry;
			unsigned base;
			unsigned size;

			int unused;  // DO NOT USE, maintained for compatibility.

			unsigned *dynamic;

			unsigned wrprotect_start;
			unsigned wrprotect_end;

			soinfo *next;
			unsigned flags;

			const char *strtab;
			Elf32_Sym *symtab;

			unsigned nbucket;
			unsigned nchain;
			unsigned *bucket;
			unsigned *chain;

			unsigned *plt_got;

			Elf32_Rel *plt_rel;
			unsigned plt_rel_count;

			Elf32_Rel *rel;
			unsigned rel_count;

			unsigned *preinit_array;
			unsigned preinit_array_count;

			unsigned *init_array;
			unsigned init_array_count;
			unsigned *fini_array;
			unsigned fini_array_count;

			void (*init_func)(void);
			void (*fini_func)(void);

#ifdef ANDROID_ARM_LINKER
			/* ARM EABI section used for stack unwinding. */
			unsigned *ARM_exidx;
			unsigned ARM_exidx_count;
#endif

			unsigned refcount;
			struct link_map linkmap;

			int constructors_called;

			Elf32_Addr gnu_relro_start;
			unsigned gnu_relro_len;

	};
	/*============================= END ANDROID CODE =============================*/

	// A class's vtable, located with dlsym("_ZTV...") on the game library.
	class VirtualTable
	{
	private:
		hook::soinfo* handle;
		void** data;

	public:
		VirtualTable(hook::soinfo* handle, std::string symbolName);

		/// @brief false when the vtable symbol does not exist (unsupported game build)
		bool Valid() const { return data != nullptr; }

		/// @brief Replaces the function at `index` (from FindIndex), making only that slot's
		/// page writable. false for an invalid index or when the page cannot be unprotected.
		bool Hook(int index, void* replacement, void** original);

		/// @brief Index of the function named `symbolName` in this vtable, or -1.
		int FindIndex(std::string symbolName);
	};
}; // namespace hook
}; // namespace mcfm
