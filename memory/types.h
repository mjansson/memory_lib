/* types.h  -  Memory library  -  Public Domain  -  2013 Mattias Jansson
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

/*! \file types.h
    Memory types */

#include <foundation/platform.h>
#include <foundation/types.h>

#include <memory/build.h>

#if defined(MEMORY_COMPILE) && MEMORY_COMPILE
#ifdef __cplusplus
#define MEMORY_EXTERN extern "C"
#define MEMORY_API extern "C"
#else
#define MEMORY_EXTERN extern
#define MEMORY_API extern
#endif
#else
#ifdef __cplusplus
#define MEMORY_EXTERN extern "C"
#define MEMORY_API extern "C"
#else
#define MEMORY_EXTERN extern
#define MEMORY_API extern
#endif
#endif
