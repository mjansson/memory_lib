/* memory.h  -  Memory library  -  Public Domain  -  2013 Mattias Jansson
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

#pragma once

/*! \file memory.h
    Memory management */

#include <memory/types.h>

MEMORY_API memory_system_t
memory_system(void);

MEMORY_API version_t
memory_module_version(void);
