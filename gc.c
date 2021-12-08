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
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gc.h"

#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <limits.h>
#include <assert.h>

#define MAP_SIZE 0x10000000000

#define GC_INTERVAL 0x100000

#define BITMAP_EMPTY 0x0
#define BITMAP_FREE  0x1
#define BITMAP_ALLOC 0x3

#define BITMAP_INDEX(ptr) bitmap[ptr - heap]

int gc_enabled;

static int gc_has_init = 0;

static uintptr_t* stack_start;

static uintptr_t* heap;
static uintptr_t* heap_top;

static uint8_t* bitmap;
static uint8_t* free_start;

static void gc_collect_root(uintptr_t);

void __gc_init(void* sp)
{
  gc_enabled = 1;
  gc_has_init = 1;
  stack_start = sp;
  free_start = bitmap = mmap(0, MAP_SIZE/8, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
  heap = heap_top     = mmap(0, MAP_SIZE,   PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
  assert(heap   != MAP_FAILED);
  assert(bitmap != MAP_FAILED);
  BITMAP_INDEX(heap) = BITMAP_FREE;
}

__attribute__((malloc, hot, assume_aligned(sizeof(uint64_t)), alloc_size(1), returns_nonnull))
void* gc_malloc(size_t bytes)
{
  assert(gc_has_init);
  static int byte_count = 0;
  byte_count += bytes;
  if (byte_count > 1024 * 1024) gc_collect(), byte_count = 0;
  const size_t longs = (bytes + 7) / sizeof(uintptr_t);
  free_start = memchr(free_start, BITMAP_FREE, LONG_MAX);
  for (uint8_t* restrict free_end; (free_end = memchr(free_start + 1, BITMAP_ALLOC, longs)); )
    free_start = memchr(free_end + 1, BITMAP_FREE, LONG_MAX);
  memset(free_start + 1, BITMAP_EMPTY, longs - 1);
  uint8_t* restrict free_end = free_start + longs;
  uintptr_t* buf = heap + (free_start - bitmap);
  *free_start = BITMAP_ALLOC;
  if (heap_top < buf + longs) heap_top = buf + longs;
  if (*free_end != BITMAP_ALLOC) *free_end = BITMAP_FREE;
  free_start = free_end;
  return buf;
}

void* gc_realloc(void* src, size_t sz)
{
  return memcpy(gc_malloc(sz), src, sz);
}

static void gc_collect_root(uintptr_t object)
{
  uintptr_t* ptr = (uintptr_t*)object;
  if (ptr < heap || ptr >= heap_top
   || BITMAP_INDEX(ptr) != BITMAP_FREE) return;
  BITMAP_INDEX(ptr) = BITMAP_ALLOC;
  for (uintptr_t i = 1; BITMAP_INDEX(ptr + i) == BITMAP_EMPTY; ++i)
    gc_collect_root(ptr[i]);
  gc_collect_root(ptr[0]);
}

__attribute__((noinline)) void gc_collect()
{
  if (!gc_enabled) return;
  for (uintptr_t* p = (uintptr_t*)bitmap; (uint8_t*)p < bitmap + (heap_top - heap); ++p)
    *p &= 0x5555555555555555;
  jmp_buf a;
  setjmp(a);
  uintptr_t* sp = (uintptr_t*)&sp;
  for (uintptr_t* root = sp + 1; root < stack_start; ++root)
    gc_collect_root(*root);
  free_start = bitmap;
}

char* gc_strdup(char* src)
{
  const size_t len = strlen(src);
  return memcpy(gc_malloc(len + 1), src, len + 1);
}

char* gc_strndup(char* src, size_t bytes)
{
  const size_t len = strlen(src);
  if (len < bytes) bytes = len;
  char* dest = gc_malloc(bytes + 1);
  dest[bytes] = '\0';
  return memcpy(dest, src, bytes);
}
