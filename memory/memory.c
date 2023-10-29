/* memory.c  -  Memory library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a cross-platform memory allocation library in C11 providing a lock-free
 * implementation of memory allocation and deallocation for projects based on our foundation library.
 * The latest source code is always available at
 *
 * https://github.com/mjansson/memory_lib
 *
 * This library is built on top of the foundation library available at
 *
 * https://github.com/mjansson/foundation_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <foundation/foundation.h>

#include "memory.h"
#include "rpmalloc.h"

#if FOUNDATION_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif

static int
memory_rpmalloc_initialize(void) {
	return rpmalloc_initialize(0);
}

static void
memory_rpmalloc_finalize(void) {
	rpmalloc_finalize();
}

static void*
memory_rpmalloc_allocate(hash_t context, size_t size, unsigned int align, unsigned int hint) {
	FOUNDATION_UNUSED(context);
	if (align <= 16) {
		void* block = rpmalloc(size);
		if (hint & MEMORY_ZERO_INITIALIZED)
			memset(block, 0, size);
		return block;
	} else {
		void* block = rpaligned_alloc(align, size);
		if (hint & MEMORY_ZERO_INITIALIZED)
			memset(block, 0, size);
		return block;
	}
}

static void*
memory_rpmalloc_reallocate(void* p, size_t size, unsigned int align, size_t oldsize, unsigned int hint) {
	FOUNDATION_ASSERT(!p || oldsize);
	void* block = rpaligned_realloc(p, align, size, oldsize, (hint & MEMORY_NO_PRESERVE) ? RPMALLOC_NO_PRESERVE : 0);
	if ((hint & MEMORY_ZERO_INITIALIZED) && block && (size > oldsize))
		memset(pointer_offset(block, oldsize), 0, (size - oldsize));
	return block;
}

static size_t
memory_rpmalloc_usable_size(const void* p) {
	return rpmalloc_usable_size((void*)p);
}

static void
memory_rpmalloc_deallocate(void* p) {
	rpfree(p);
}

static void
memory_rpmalloc_thread_initialize(void) {
	rpmalloc_thread_initialize();
}

static void
memory_rpmalloc_thread_finalize(void) {
	rpmalloc_thread_finalize();
}

memory_system_t
memory_system(void) {
	memory_system_t memsystem;
	memset(&memsystem, 0, sizeof(memsystem));
	memsystem.allocate = memory_rpmalloc_allocate;
	memsystem.reallocate = memory_rpmalloc_reallocate;
	memsystem.deallocate = memory_rpmalloc_deallocate;
	memsystem.initialize = memory_rpmalloc_initialize;
	memsystem.usable_size = memory_rpmalloc_usable_size;
	memsystem.finalize = memory_rpmalloc_finalize;
	memsystem.thread_initialize = memory_rpmalloc_thread_initialize;
	memsystem.thread_finalize = memory_rpmalloc_thread_finalize;
	return memsystem;
}
