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


typedef struct ALIGN(16) _memory_statistics
{
	//Statistics below are available if BUILD_ENABLE_MEMORY_STATISTICS > 0
	volatile int64_t             allocated_current_raw;
	volatile int64_t             allocated_total_raw;
	volatile int64_t             allocations_current_raw;
	volatile int64_t             allocations_total_raw;

	volatile int64_t             allocated_current;
	volatile int64_t             allocated_total;
	volatile int64_t             allocations_current;
	volatile int64_t             allocations_total;

	//Statistics below are available if BUILD_ENABLE_MEMORY_STATISTICS > 1
	volatile int64_t             allocations_new_descriptor_superblock;
	volatile int64_t             allocations_new_descriptor_superblock_deallocations;

	volatile int64_t             allocations_calls_new_block;
	volatile int64_t             allocations_new_block_earlyouts;
	volatile int64_t             allocations_new_block_superblock;
	volatile int64_t             allocations_new_block_pending_hits;
	volatile int64_t             allocations_new_block_superblock_success;
	volatile int64_t             allocations_new_block_pending_success;
	volatile int64_t             allocations_new_block_superblock_deallocations;
	volatile int64_t             allocations_new_block_pending_deallocations;
	volatile int64_t             allocations_new_block_superblock_stores;
	volatile int64_t             allocations_new_block_pending_stores;

	volatile int64_t             allocations_calls_oversize;
	volatile int64_t             allocations_calls_heap;
	volatile int64_t             allocations_calls_heap_loops;
	volatile int64_t             allocations_calls_heap_pool[BUILD_SIZE_HEAP_THREAD_POOL];
	
	volatile int64_t             allocations_calls_active;
	volatile int64_t             allocations_calls_active_no_active;
	volatile int64_t             allocations_calls_active_to_partial;
	volatile int64_t             allocations_calls_active_to_active;
	volatile int64_t             allocations_calls_active_to_full;
	volatile int64_t             allocations_calls_active_credits;
	
	volatile int64_t             allocations_calls_partial;
	volatile int64_t             allocations_calls_partial_tries;
	volatile int64_t             allocations_calls_partial_no_descriptor;
	volatile int64_t             allocations_calls_partial_to_retire;
	volatile int64_t             allocations_calls_partial_to_active;
	volatile int64_t             allocations_calls_partial_to_full;
} memory_statistics_t;
