/* types.h  -  Memory library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a cross-platform memory allocation library in C11 providing a lock-free
 * implementation of memory allocation and deallocation for projects based on our foundation library.
 * The latest source code is always available at
 *
 * https://github.com/rampantpixels/memory_lib
 *
 * This library is built on top of the foundation library available at
 *
 * https://github.com/rampantpixels/foundation_lib
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


#if defined( MEMORY_COMPILE ) && MEMORY_COMPILE
#  ifdef __cplusplus
#  define MEMORY_EXTERN extern "C"
#  define MEMORY_API extern "C"
#  else
#  define MEMORY_EXTERN extern
#  define MEMORY_API extern
#  endif
#else
#  ifdef __cplusplus
#  define MEMORY_EXTERN extern "C"
#  define MEMORY_API extern "C"
#  else
#  define MEMORY_EXTERN extern
#  define MEMORY_API extern
#  endif
#endif

typedef struct memory_statistics_detail_t memory_statistics_detail_t;

struct memory_statistics_detail_t {
	/*! Number of allocations in total, running counter */
	uint64_t allocations_total;
	/*! Number of allocations, current */
	uint64_t allocations_current;
	/*! Number of allocated bytes in total, running counter */
	uint64_t allocated_total;
	/*! Number of allocated bytes, current */
	uint64_t allocated_current;
	/*! Number of allocations in total of OS virtual memory pages, running counter */
	uint64_t allocations_total_virtual;
	/*! Number of allocations of OS virtual memory pages, current */
	uint64_t allocations_current_virtual;
	/*! Number of allocated bytes total of OS virtual memory pages, running counter */
	uint64_t allocated_total_virtual;
	/*! Number of allocated bytes of OS virtual memory pages, current */
	uint64_t allocated_current_virtual;
};
