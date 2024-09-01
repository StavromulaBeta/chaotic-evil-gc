/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2021, Finn Barber
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND uint64_t EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR uint64_t DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON uint64_t THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN uint64_t WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gc.h"

#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <limits.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

#define unlikely(expr) (__builtin_expect((_Bool)(expr), 0))
#define likely(expr)	 (__builtin_expect((_Bool)(expr), 1))

#define PAGE_SIZE 4096

#define EMPTY     0x0
#define ALLOC     0x1
#define FLATALLOC 0x2
#define FORWARD   0x3

static size_t system_memory;
static uint64_t* space[2] = {NULL,NULL};
static char* bitmap[2]    = {NULL,NULL};
static size_t alloc[2]    = {0, 0};
static _Bool z = 0;
static uint64_t* function_stack_start;

__attribute__((hot))
static _Bool is_gc_ptr(uint64_t object)
{
	return !(object & 0xffff000000000007 || (uint64_t*)object - space[!z] >= alloc[!z]);
}

void __gc_init(void* start)
{
	function_stack_start = start;
	system_memory = sysconf(_SC_PHYS_PAGES) * 4096;
	bitmap[0] = mmap(0, system_memory/18, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE, -1, 0);
	bitmap[1] = mmap(0, system_memory/18, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE, -1, 0);
	space[0]  = mmap(0, (system_memory/18)*8, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE, -1, 0);
	space[1]  = mmap(0, (system_memory/18)*8, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE, -1, 0);
	bitmap[0][0] = ALLOC;
	bitmap[1][0] = ALLOC;
}

__attribute__((hot, assume_aligned(sizeof(uint64_t)), alloc_size(1), returns_nonnull))
void* gc_malloc(size_t sz)
{
	static ptrdiff_t interval = 1024l*1024l*10;
	interval -= sz;
	if unlikely(interval < 0)
	{
		gc_collect();
		interval = 1024l*1024l*10l + alloc[z] * 6;
	}
	void* buf = space[z] + alloc[z];
	//assert(bitmap[z][alloc[z]] == ALLOC);
	alloc[z] += (sz + 7) / 8;
	//assert(!sz || bitmap[z][alloc[z]] == EMPTY);
	bitmap[z][alloc[z]] = ALLOC;
	//assert(!((uint64_t)buf & 7));
	return buf;
}

__attribute__((hot, assume_aligned(sizeof(uint64_t)), alloc_size(1), returns_nonnull))
void* gc_flatmalloc(size_t sz)
{
	static ptrdiff_t interval = 1024l*1024l*10;
	interval -= sz;
	if unlikely(interval < 0)
	{
		gc_collect();
		interval = 1024l*1024l*10l + alloc[z] * 6;
	}
	void* buf = space[z] + alloc[z];
	//assert(bitmap[z][alloc[z]] == ALLOC);
	alloc[z] += (sz + 7) / 8;
	//assert(!sz || bitmap[z][alloc[z]] == EMPTY);
	bitmap[z][alloc[z]] = FLATALLOC;
	//assert(!((uint64_t)buf & 7));
	return buf;
}

__attribute__((hot))
static void gc_collect_root(uint64_t* restrict addr)
{
	if (!is_gc_ptr(*addr)) return;
	struct action {
		uint64_t from;
		uint64_t* restrict to;
	};
	struct action* restrict act_stk_start = (struct action*)space[!z] + alloc[!z];
	struct action* restrict act_stk_top = act_stk_start;
	*act_stk_top++ = (struct action) { .from=*addr, .to=addr };
	while (act_stk_top-- != act_stk_start)
	{
		uint64_t from = act_stk_top->from;
		uint64_t* to = act_stk_top->to;
		uint64_t index = (uint64_t*)from - space[!z];
		ptrdiff_t offset = 0;
		while (bitmap[!z][index] == EMPTY) index--, offset++; // Ptr to middle of object
		if (bitmap[!z][index] == FORWARD)
			*to = (uint64_t)((uint64_t*)space[!z][index] + offset);
		else
		{
			_Bool flat = bitmap[!z][index] == FLATALLOC;
			//assert(bitmap[!z][index] == ALLOC);
			uint64_t* buf = space[z] + alloc[z]; // Buffer in newspace
			//assert(bitmap[z][alloc[z]] == ALLOC);
			size_t sz = 1;
			for (;bitmap[!z][index+sz] == EMPTY;sz++);
			alloc[z] += sz;
			//assert(bitmap[z][alloc[z]] == EMPTY);
			bitmap[z][alloc[z]] = bitmap[!z][index];
			for (size_t i = 0;i < sz;i++)
			{
				uint64_t from = space[!z][index+i];
				if (!flat && is_gc_ptr(from))
					*act_stk_top++ = (struct action) { .from=from, .to=buf+i };
				else buf[i] = from;
			}
			space[!z][index] = (uint64_t)buf; // Set forwarding address
			bitmap[!z][index] = FORWARD;
			*to = (uint64_t)(buf + offset);
		}
	}
}

__attribute__((noinline,hot)) void gc_collect(void)
{
	/*
	clock_t start, end;
	double cpu_time_used;
	size_t heapsz = alloc[z];
	start = clock();
	*/

	z = !z;
	memset(bitmap[z], EMPTY, alloc[z]+1);
	alloc[z] = 0;
	bitmap[z][0] = ALLOC;

	jmp_buf a;
	if (setjmp(a)) return;

	for (uint64_t* root = (uint64_t*)&a; root < (uint64_t*)function_stack_start; ++root)
		gc_collect_root(root); // Watch me destructively modify the call stack

	/*
	end = clock();
	printf("%lf seconds for %ziMB -> %ziMB\n", (double)(end - start) / CLOCKS_PER_SEC, heapsz * 8 /1024/1024, alloc[z] * 8 / 1024/1024);
	*/

	longjmp(a, 1);
}

char* gc_strdup(char* src)
{
	const size_t len = strlen(src);
	return memcpy(gc_flatmalloc(len + 1), src, len + 1);
}

char* gc_strndup(char* src, size_t bytes)
{
	const size_t len = strlen(src);
	if (len < bytes) bytes = len;
	char* dest = gc_flatmalloc(bytes + 1);
	dest[bytes] = '\0';
	return memcpy(dest, src, bytes);
}
