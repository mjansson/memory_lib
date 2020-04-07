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

static int
_memory_initialize(void) {
	return rpmalloc_initialize();
}

static void
_memory_finalize(void) {
	rpmalloc_finalize();
}

static void*
_memory_allocate(hash_t context, size_t size, unsigned int align, unsigned int hint) {
	FOUNDATION_UNUSED(context);
	void* block = rpmemalign(align, size);
	if ((hint & MEMORY_ZERO_INITIALIZED) && block)
		memset(block, 0, size);
	return block;
}

static void*
_memory_reallocate(void* p, size_t size, unsigned int align, size_t oldsize, unsigned int hint) {
	return rpaligned_realloc(p, align, size, oldsize, (hint & MEMORY_NO_PRESERVE) ? RPMALLOC_NO_PRESERVE : 0);
}

static void
_memory_deallocate(void* p) {
	rpfree(p);
}

static void
_memory_thread_initialize(void) {
	rpmalloc_thread_initialize();
}

static void
_memory_thread_finalize(void) {
	rpmalloc_thread_finalize();
}

memory_system_t
memory_system(void) {
	memory_system_t memsystem;
	memset(&memsystem, 0, sizeof(memsystem));
	memsystem.allocate = _memory_allocate;
	memsystem.reallocate = _memory_reallocate;
	memsystem.deallocate = _memory_deallocate;
	memsystem.initialize = _memory_initialize;
	memsystem.finalize = _memory_finalize;
	memsystem.thread_initialize = _memory_thread_initialize;
	memsystem.thread_finalize = _memory_thread_finalize;
	return memsystem;
}
