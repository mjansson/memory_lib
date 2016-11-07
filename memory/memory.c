/* memory.c  -  Memory library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

#include <foundation/foundation.h>
#include <foundation/windows.h>
#include <memory/memory.h>
#include <memory/log.h>

#if FOUNDATION_PLATFORM_POSIX
#  include <sys/mman.h>
#  ifndef MAP_UNINITIALIZED
#    define MAP_UNINITIALIZED 0
#  endif
#  ifndef MAP_ANONYMOUS
#    define MAP_ANONYMOUS MAP_ANON
#  endif
#endif

//Main entry points
static void*
_memory_allocate(hash_t context, size_t size, unsigned int align, unsigned int hint);

static void
_memory_deallocate(void* p);

//Thresholds

//! Minimum number of spans in thread cache after releasing spans to global cache
#define MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS    8

//! Maximumm number of spans in thread cache before releasing spans to global cache
#define MAXIMUM_NUMBER_OF_THREAD_CACHE_SPANS    (MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS*2)

//! Maximum number of spans in global cache before starting to release pages back to system
#define MAXIMUM_NUMBER_OF_GLOBAL_CACHE_SPANS    (MAXIMUM_NUMBER_OF_THREAD_CACHE_SPANS*16)


#define MEMORY_ASSERT       FOUNDATION_ASSERT
//#define MEMORY_ASSERT(...)

#define PAGE_SIZE           4096
#define PAGE_ADDRESS_GRANULARITY 65536
#define MAX_CHUNK_SIZE      (PAGE_ADDRESS_GRANULARITY)
#define MAX_PAGE_COUNT      (MAX_CHUNK_SIZE/PAGE_SIZE)
#define PAGE_MASK           (~(MAX_CHUNK_SIZE - 1))

#define CHUNK_HEADER_SIZE   16

#define SMALL_GRANULARITY   16
#define SMALL_CLASS_COUNT   (((PAGE_SIZE - CHUNK_HEADER_SIZE) / 2) / SMALL_GRANULARITY)
#define SMALL_SIZE_LIMIT    (SMALL_CLASS_COUNT * SMALL_GRANULARITY)

#define MEDIUM_CLASS_COUNT  32
#define MEDIUM_SIZE_INCR_UNALIGNED ((MAX_CHUNK_SIZE - (CHUNK_HEADER_SIZE * 2) - SMALL_SIZE_LIMIT) / MEDIUM_CLASS_COUNT)
#define MEDIUM_SIZE_INCR    (MEDIUM_SIZE_INCR_UNALIGNED - (MEDIUM_SIZE_INCR_UNALIGNED % 16))
#define MEDIUM_SIZE_LIMIT   (SMALL_SIZE_LIMIT + (MEDIUM_CLASS_COUNT * MEDIUM_SIZE_INCR))

#define SIZE_CLASS_COUNT    (SMALL_CLASS_COUNT + MEDIUM_CLASS_COUNT)

#define SPAN_CLASS_COUNT    (MAX_CHUNK_SIZE / PAGE_SIZE)

#define SPAN_LIST_LOCK_TOKEN ((void*)1)

#define pointer_offset_pages(ptr, offset) (pointer_offset((ptr), (uintptr_t)(offset) * PAGE_ADDRESS_GRANULARITY))
#define pointer_diff_pages(a, b) ((int32_t)(pointer_diff((a), (b)) / PAGE_ADDRESS_GRANULARITY))

typedef struct size_class_t size_class_t;
typedef struct tag_t tag_t;
typedef struct chunk_t chunk_t;
typedef struct heap_t heap_t;
typedef struct span_t span_t;

/* ASSUMPTIONS
   ===========
   Assumes all virtual memory pages allocated are on a PAGE_ADDRESS_GRANULARITY
   alignment, meaning PAGE_MASK-1 bits are always zero for a page virtual
   memory address.

   Chunk offsets as signed offset in number of page address granularity steps
   (actual offset is PAGE_ADDRESS_GRANULARITY * next_chunk). This assumes
   the maximum distance in memory address space between two different pages
   is less than 2^48 (32bit integer offset, 16bit address granularity)
*/

//! Size class descriptor
struct size_class_t {
	//! Size of blocks in this class
	uint32_t size;
	//! Number of pages to allocate for a chunk
	uint16_t page_count;
	//! Number of blocks in each chunk
	uint16_t block_count;
	//! Number of free blocks in all chunks
	uint16_t free_count;
	//! First free chunk for this class (offset)
	int32_t free_chunk_list;
	//! First full chunk for this class (offset)
	int32_t full_chunk_list;
};

//! Memory chunk descriptor
struct chunk_t {
	//! Next chunk in linked list (offset)
	int32_t next_chunk;
	//! Previous chunk in linked list (offset)
	int32_t prev_chunk;
	//! Size class index
	uint8_t size_class;
	//! Total number of blocks in chunk
	uint8_t block_count;
	//! Number of free blocks in chunk
	uint8_t free_count;
	//! First free block
	uint8_t free_list;
	//! Heap ID
	uint16_t heap_id;
	//! Linked list of blocks (as offsets from that block minus one, meaning 0 is next consecutive block in memory)
	//  TODO: Could be stored as a bitmap, with free list as index of 8-bit pieces with at least one free block
	int8_t next_block[];
};

//! Thread heap
struct heap_t {
	//! Size classes
	size_class_t size_class[SIZE_CLASS_COUNT];
	//! Free spans
	span_t* span_cache[SPAN_CLASS_COUNT];
	//! Next heap in list
	heap_t* next_heap;
	//! Heap ID
	uint16_t id;
	//! Deallocate delegation list
	atomicptr_t delegated_deallocate;
};

//! Span of pages
struct span_t {
	//! Next span
	span_t* next_span;
	//! Skip span (for use in global span cache)
	span_t* skip_span;
	//! Total number of spans in span cache
	size_t total_span_count;
	//! Total number of pages in span cache
	size_t total_page_count;
};
FOUNDATION_STATIC_ASSERT(sizeof(heap_t) <= PAGE_SIZE, "Invalid heap size");

FOUNDATION_DECLARE_THREAD_LOCAL(heap_t*, heap, 0)

typedef struct memory_statistics_atomic_t memory_statistics_atomic_t;
struct memory_statistics_atomic_t {
	/*! Number of allocations in total, running counter */
	atomic64_t allocations_total;
	/*! Number fo allocations, current */
	atomic64_t allocations_current;
	/*! Number of allocated bytes in total, running counter */
	atomic64_t allocated_total;
	/*! Number of allocated bytes, current */
	atomic64_t allocated_current;
	/*! Number of allocated bytes in total (including overhead), running counter */
	atomic64_t allocated_total_raw;
	/*! Number of allocated bytes (including overhead), current */
	atomic64_t allocated_current_raw;
};
FOUNDATION_STATIC_ASSERT(sizeof(memory_statistics_detail_t) == sizeof(memory_statistics_atomic_t),
                         "Statistics size mismatch");

static atomic32_t _memory_heap_id;
static atomicptr_t _memory_span_cache[SPAN_CLASS_COUNT];

#if FOUNDATION_PLATFORM_POSIX
static atomic64_t _memory_addr;
#endif

#define HEAP_ARRAY_SIZE 197
static heap_t* _memory_heaps[HEAP_ARRAY_SIZE];
static mutex_t* _memory_heaps_lock;
static atomicptr_t _memory_delegated_deallocate;

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
static memory_statistics_atomic_t _memory_statistics;
#endif

static size_t
_memory_chunk_header_size(size_t block_count) {
	size_t header_size = sizeof(chunk_t) + block_count;
	size_t misalignment = header_size % CHUNK_HEADER_SIZE;
	return header_size + (misalignment ? (CHUNK_HEADER_SIZE - misalignment) : 0);
}

#if FOUNDATION_PLATFORM_WINDOWS
typedef long (*NtAllocateVirtualMemoryFn)(HANDLE, void**, ULONG, size_t*, ULONG, ULONG);
static NtAllocateVirtualMemoryFn NtAllocateVirtualMemory = 0;
#endif

static size_t
_memory_heap_cache_insert(heap_t* heap, void* ptr, size_t page_count) {
	span_t* span = ptr;
	span_t* next_span = heap->span_cache[page_count];
	span->next_span = next_span;
	if (next_span) {
		span->total_span_count = next_span->total_span_count + 1;
		span->total_page_count = next_span->total_page_count + page_count;
	}
	else {
		span->total_span_count = 1;
		span->total_page_count = page_count;
	}
	MEMORY_ASSERT(span->total_page_count == (span->total_span_count * page_count));
	heap->span_cache[page_count] = span;
	
	return span->total_span_count;
}

static span_t*
_memory_heap_cache_extract(heap_t* heap, size_t page_count) {
	if (heap->span_cache[page_count]) {
		span_t* span = heap->span_cache[page_count];
		heap->span_cache[page_count] = span->next_span;
		return span;
	}
	return 0;
}

static span_t*
_memory_global_cache_insert(span_t* span, size_t page_count) {
	size_t num_spans_to_release = MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS;
	span_t* first_span = span;
	span_t* last_span = span;
	for (size_t ispan = 0; ispan < num_spans_to_release - 1; ++ispan)
		last_span = last_span->next_span;

	span_t* global_span;
	span_t* restore_span;
	atomic_thread_fence_acquire();
	global_span = atomic_load_ptr(&_memory_span_cache[page_count]);
	restore_span = last_span->next_span;
	while (true) {
		if (global_span != SPAN_LIST_LOCK_TOKEN) {
			uintptr_t global_span_count = (uintptr_t)global_span & (~PAGE_MASK);
			MEMORY_ASSERT((global_span_count % MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS) == 0);

			last_span->next_span = (span_t*)((void*)((uintptr_t)global_span & (uintptr_t)PAGE_MASK));
			span->skip_span = last_span->next_span;
			first_span = (span_t*)((void*)((uintptr_t)span | (global_span_count + num_spans_to_release)));

			if (atomic_cas_ptr(&_memory_span_cache[page_count], first_span, global_span)) {
				//Span is inserted into global cache, return reminder
				return restore_span;
			}

			if (global_span_count >= MAXIMUM_NUMBER_OF_GLOBAL_CACHE_SPANS) {
				//Failed to insert and global cache is full, restore and return full list
				last_span->next_span = restore_span;
				break;
			}
		}
		else {
			thread_yield();
			atomic_thread_fence_acquire();
		}

		//Load new cache pointer and retry
		global_span = atomic_load_ptr(&_memory_span_cache[page_count]);
	}

	return span;
}

static span_t*
_memory_global_cache_extract(size_t page_count) {
	span_t* span = 0;
	atomic_thread_fence_acquire();
	span_t* global_span = atomic_load_ptr(&_memory_span_cache[page_count]);
	while (global_span) {
		if ((global_span != (span_t*)SPAN_LIST_LOCK_TOKEN) &&
		        atomic_cas_ptr(&_memory_span_cache[page_count], SPAN_LIST_LOCK_TOKEN, global_span)) {
			//Grab minimum number of thread cache spans, using the skip_span pointer to quickly
			//skip ahead in the list to get the new head
			uintptr_t global_span_count = (uintptr_t)global_span & (~PAGE_MASK);
			if (global_span_count > 0) {
				span = (span_t*)((void*)((uintptr_t)global_span & (uintptr_t)PAGE_MASK));

				MEMORY_ASSERT(global_span_count >= MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS);
				MEMORY_ASSERT((global_span_count % MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS) == 0);

				span_t* new_global_span = span->skip_span;
				global_span_count -= MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS;

				span_t* new_cache_head = global_span_count ?
				                         (span_t*)((void*)((uintptr_t)new_global_span | global_span_count)) :
				                         0;
				atomic_store_ptr(&_memory_span_cache[page_count], new_cache_head);
				atomic_thread_fence_release();
			}
			break;
		}
		else {
			thread_yield();
			atomic_thread_fence_acquire();
		}

		global_span = atomic_load_ptr(&_memory_span_cache[page_count]);
	}

	if (span) {
		span_t* first_cache_span = span->next_span;
		span_t* next_span = first_cache_span;
		span_t* last_span = next_span;
		for (size_t ispan = MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS - 1; ispan > 0; --ispan) {
			next_span->total_span_count = ispan;
			next_span->total_page_count = ispan * page_count;
			last_span = next_span;
			next_span = next_span->next_span;
		}
		last_span->next_span = 0;
	}

	return span;
}

static void*
_memory_map(size_t page_count) {
	void* pages_ptr = 0;

#if FOUNDATION_PLATFORM_WINDOWS
	pages_ptr = VirtualAlloc(0, PAGE_SIZE * page_count, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	MEMORY_ASSERT(!((uintptr_t)pages_ptr & ~(uintptr_t)PAGE_MASK));

	/* For controlling high order bits
	long vmres = NtAllocateVirtualMemory(((HANDLE)(LONG_PTR)-1), &pages_ptr, 1, &allocate_size,
	                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	*/
#else
	do {
		void* base_addr = (void*)(uintptr_t)atomic_exchange_and_add64(&_memory_addr, 0x10000LL);
		pages_ptr = mmap(base_addr, PAGE_SIZE * page_count, PROT_READ | PROT_WRITE,
	                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED, -1, 0);
		if (!((uintptr_t)pages_ptr & ~(uintptr_t)PAGE_MASK)) {
			if (pages_ptr != base_addr) {
				atomic_store64(&_memory_addr, (int64_t)((uintptr_t)pages_ptr) + 0x10000LL);
				atomic_thread_fence_release();
			}
			break;
		}
		munmap(pages_ptr, PAGE_SIZE * page_count);
	} while (true);
#endif

	return pages_ptr;
}

static void
_memory_unmap(void* ptr, size_t page_count) {
#if FOUNDATION_PLATFORM_WINDOWS
	FOUNDATION_UNUSED(page_count);
	VirtualFree(ptr, 0, MEM_RELEASE);
#endif
#if FOUNDATION_PLATFORM_POSIX
	munmap(ptr, PAGE_SIZE * page_count);
#endif	
}

static void*
_memory_allocate_raw(heap_t* heap, size_t page_count) {
	//Check if we can find a span that matches the request
	if (heap && (page_count < SPAN_CLASS_COUNT)) {
		span_t* span = _memory_heap_cache_extract(heap, page_count);
		if (span)
			return span;

		span = _memory_global_cache_extract(page_count);
		if (span) {
			heap->span_cache[page_count] = span->next_span;
			return span;
		}
	}

	return _memory_map(page_count);
}

static void
_memory_deallocate_raw(heap_t* heap, void* ptr, size_t page_count) {
	if (heap && (page_count < SPAN_CLASS_COUNT)) {
		//Insert into heap span cache
		size_t thread_cache_count = _memory_heap_cache_insert(heap, ptr, page_count);
		if (thread_cache_count <= MAXIMUM_NUMBER_OF_THREAD_CACHE_SPANS)
			return;

		//Release the head of the span cache to global cache
		span_t* new_head = _memory_global_cache_insert(heap->span_cache[page_count], page_count);
		if (heap->span_cache[page_count] != new_head) {
			heap->span_cache[page_count] = new_head;
			return;
		}

		//Failed to release to global span cache, pop pages from local cache again and fall through
		//and release back to system
		heap->span_cache[page_count] = heap->span_cache[page_count]->next_span;
	}

	_memory_unmap(ptr, page_count);
}

static int32_t
_memory_heap_add_chunk_to_list(heap_t* heap, int32_t list_head, chunk_t* chunk) {
	if (list_head) {
		chunk_t* next_chunk = pointer_offset_pages(heap, list_head);
		chunk->next_chunk = (int32_t)pointer_diff_pages(next_chunk, chunk);
		next_chunk->prev_chunk = (int32_t)pointer_diff_pages(chunk, next_chunk);
	}
	else {
		chunk->next_chunk = 0;
	}
	list_head = (int32_t)pointer_diff_pages(chunk, heap);
	MEMORY_ASSERT(pointer_offset_pages(heap, list_head) == chunk);
	return list_head;
}

static int32_t
_memory_heap_remove_chunk_from_list(heap_t* heap, int32_t list_head, chunk_t* chunk) {
	int32_t chunk_offset = (int32_t)pointer_diff_pages(chunk, heap);
	if (list_head == chunk_offset) {
		if (chunk->next_chunk) {
			chunk_t* next_chunk = (chunk_t*)pointer_offset_pages(chunk, chunk->next_chunk);
			list_head = (int32_t)pointer_diff_pages(next_chunk, heap);
			MEMORY_ASSERT(pointer_offset_pages(heap, list_head) == next_chunk);
		}
		else {
			list_head = 0;
		}
	}
	else {
		//If this chunk was not first in free_chunk_list, it must have a previous chunk in list
		MEMORY_ASSERT(chunk->prev_chunk);
		chunk_t* prev_chunk = (chunk_t*)pointer_offset_pages(chunk, chunk->prev_chunk);
		if (chunk->next_chunk) {
			chunk_t* next_chunk = (chunk_t*)pointer_offset_pages(chunk, chunk->next_chunk);
			prev_chunk->next_chunk = (int32_t)pointer_diff_pages(next_chunk, prev_chunk);
			next_chunk->prev_chunk = (int32_t)pointer_diff_pages(prev_chunk, next_chunk);
		}
		else {
			prev_chunk->next_chunk = 0;
		}
	}
	return list_head;
}

static void*
_memory_allocate_from_heap(heap_t* heap, size_t size) {
	size_t class_idx = (size ? (size-1) : 0) / SMALL_GRANULARITY;
	if (class_idx >= SMALL_CLASS_COUNT) {
		MEMORY_ASSERT(size > SMALL_SIZE_LIMIT);
		class_idx = SMALL_CLASS_COUNT + ((size - (1 + SMALL_SIZE_LIMIT)) / MEDIUM_SIZE_INCR);
	}

	size_class_t* size_class = heap->size_class + class_idx;
	while (size_class->size < size)
		size_class = heap->size_class + (++class_idx);

	size_t header_size = _memory_chunk_header_size(size_class->block_count);

	chunk_t* chunk = 0;
	if (size_class->free_chunk_list) {
		chunk = pointer_offset_pages(heap, size_class->free_chunk_list);
		MEMORY_ASSERT(chunk->free_count);
	}
	if (!chunk) {
		//Allocate a memory page as new chunk
		log_memory_spamf("Allocated a new chunk for size class %" PRIsize " (size %" PRIsize " -> %" PRIsize
		                 ") : %" PRIsize " bytes, %" PRIsize " blocks\n",
		                 class_idx, size, (size_t)size_class->size, PAGE_SIZE * (size_t)size_class->page_count,
		                 (size_t)size_class->block_count);
		chunk = _memory_allocate_raw(heap, size_class->page_count);
		MEMORY_ASSERT(((uintptr_t)chunk & ~PAGE_MASK) == 0);
		memset(chunk, 0, header_size);
		chunk->size_class = (uint8_t)class_idx;
		chunk->block_count = (uint8_t)size_class->block_count;
		chunk->free_count = chunk->block_count;
		chunk->heap_id = heap->id;

		size_class->free_count += size_class->block_count;
		size_class->free_chunk_list = pointer_diff_pages(chunk, heap);
		MEMORY_ASSERT(pointer_offset_pages(heap, size_class->free_chunk_list) == chunk);
	}

	MEMORY_ASSERT(chunk->free_count);
	MEMORY_ASSERT(size_class->free_count);

	//Allocate from chunk
	int block_idx = chunk->free_list;
	int next_offset = chunk->next_block[block_idx] + 1;
	--chunk->free_count;
	--size_class->free_count;

	if (!chunk->free_count) {
		//Move chunk to start of full chunks
		size_class->free_chunk_list = _memory_heap_remove_chunk_from_list(heap, size_class->free_chunk_list, chunk);
		size_class->full_chunk_list = _memory_heap_add_chunk_to_list(heap, size_class->full_chunk_list, chunk);
		MEMORY_ASSERT(size_class->full_chunk_list != size_class->free_chunk_list);
	}
	else {
		MEMORY_ASSERT(next_offset);
		chunk->free_list = (uint8_t)(block_idx + next_offset);
	}

	log_memory_spamf("Allocated block %d of %u, new first free block is %u (remains %u)\n",
	                 block_idx + 1, (unsigned int)chunk->block_count, (unsigned int)chunk->free_list,
	                 (unsigned int)chunk->free_count);

	size_t offset = header_size + ((size_t)block_idx * size_class->size);
	MEMORY_ASSERT(offset < MAX_CHUNK_SIZE);

	void* allocated_memory = pointer_offset(chunk, offset);
	log_memory_spamf("Allocated pointer is %" PRIfixPTR " (%" PRIsize " -> %u bytes, offset %" PRIsize
	                 ")\n", (uintptr_t)allocated_memory, size, size_class->size, offset);

	return allocated_memory;
}

static void
_memory_deallocate_from_heap(heap_t* heap, chunk_t* chunk, void* p) {
	size_t class_idx = chunk->size_class;
	size_class_t* size_class = heap->size_class + class_idx;

	size_t header_size = _memory_chunk_header_size(chunk->block_count);

	void* block_start = pointer_offset(chunk, header_size);
	size_t block_idx = (size_t)pointer_diff(p, block_start) / size_class->size;

	bool was_full = !chunk->free_count;

	if (was_full) {
		//Remove chunk from list of full chunks
		size_class->full_chunk_list = _memory_heap_remove_chunk_from_list(heap, size_class->full_chunk_list, chunk);
	}

	++size_class->free_count;
	++chunk->free_count;
	if (chunk->free_count == chunk->block_count) {
		//If chunk went from partial free -> free, remove it from the free_chunk_list
		if (!was_full) {
			size_class->free_chunk_list = _memory_heap_remove_chunk_from_list(heap, size_class->free_chunk_list, chunk);
			MEMORY_ASSERT((!size_class->full_chunk_list && !size_class->free_chunk_list) ||
			              (size_class->full_chunk_list != size_class->free_chunk_list));
		}

		size_class->free_count -= size_class->block_count;

		_memory_deallocate_raw(heap, chunk, size_class->page_count);

		return;
	}

	//Link in block in free list (0 offset means next block, hence the +1 in the subtraction)
	chunk->next_block[block_idx] = (int8_t)chunk->free_list - ((int8_t)block_idx + 1);
	chunk->free_list = (uint8_t)block_idx;

	//Check if we changed state from full -> partial free
	if (!was_full) {
		MEMORY_ASSERT(size_class->free_chunk_list);
		return;
	}

	//Chunk went from full -> partial free, add to list
	size_class->free_chunk_list = _memory_heap_add_chunk_to_list(heap, size_class->free_chunk_list, chunk);
	MEMORY_ASSERT(size_class->full_chunk_list != size_class->free_chunk_list);
}

static void
_memory_deallocate_from_other_heap(heap_t* heap, chunk_t* chunk, void* p) {
	//Delegate to correct thread, or adopt if orphaned and we have a heap
	//If neither, delegate to global orphaned list
	if (_memory_heaps_lock)
		mutex_lock(_memory_heaps_lock);

	size_t list_idx = chunk->heap_id % HEAP_ARRAY_SIZE;
	heap_t* other_heap = _memory_heaps[list_idx];
	while (!other_heap || (other_heap->id != chunk->heap_id))
		other_heap = other_heap->next_heap;
	if (other_heap) {
		//Delegate to heap
		void* last_ptr;
		void* new_ptr = p;
		do {
			last_ptr = atomic_load_ptr(&other_heap->delegated_deallocate);
			*(void**)p = last_ptr;
		} while (!atomic_cas_ptr(&other_heap->delegated_deallocate, new_ptr, last_ptr));
		chunk = 0;
	}

	if (_memory_heaps_lock)
		mutex_unlock(_memory_heaps_lock);

	if (chunk) {
		if (heap) {
			//Adopt chunk and free block
			size_t class_idx = chunk->size_class;
			size_class_t* size_class = heap->size_class + class_idx;
			if (chunk->free_count)
				size_class->free_chunk_list = _memory_heap_add_chunk_to_list(heap, size_class->free_chunk_list, chunk);
			else
				size_class->full_chunk_list = _memory_heap_add_chunk_to_list(heap, size_class->full_chunk_list, chunk);
			MEMORY_ASSERT(size_class->full_chunk_list != size_class->free_chunk_list);
			_memory_deallocate_from_heap(heap, chunk, p);
		}
		else {
			//Delegate to global orhpan list
			void* last_ptr;
			void* new_ptr = p;
			do {
				last_ptr = atomic_load_ptr(&_memory_delegated_deallocate);
				*(void**)p = last_ptr;
			} while (!atomic_cas_ptr(&_memory_delegated_deallocate, new_ptr, last_ptr));
		}
	}
}

static void
_memory_deallocate_delegated(heap_t* heap) {
	atomic_thread_fence_acquire();
	void* p = atomic_load_ptr(&heap->delegated_deallocate);
	if (p && atomic_cas_ptr(&heap->delegated_deallocate, 0, p)) {
		do {
			void* next = *(void**)p;
			_memory_deallocate(p);
			p = next;
		} while (p);
	}

	p = atomic_load_ptr(&_memory_delegated_deallocate);
	if (p && atomic_cas_ptr(&_memory_delegated_deallocate, 0, p)) {
		do {
			void* next = *(void**)p;
			_memory_deallocate(p);
			p = next;
		} while (p);
	}
}

static void
_memory_adjust_size_class(heap_t* heap, size_t iclass) {
	size_t block_size = heap->size_class[iclass].size;
	size_t block_count = ((PAGE_SIZE - sizeof(chunk_t)) / (block_size + 1));
	size_t header_size = _memory_chunk_header_size(block_count);
	if (!block_count)
		++block_count;
	if (header_size + (block_size * block_count) > PAGE_SIZE)
		--block_count;
	size_t wasted = (PAGE_SIZE - header_size) - (block_size * block_count);

	size_t page_size_counter = 1;
	size_t overhead = wasted + header_size;

	float current_factor = (float)overhead / ((float)block_count * (float)block_size);
	float best_factor = current_factor;
	size_t best_wasted = wasted;
	size_t best_overhead = overhead;
	size_t best_page_count = page_size_counter;
	size_t best_block_count = block_count;

	while ((((float)wasted / (float)block_count) > ((float)block_size / 32.0f))) {
		size_t page_size = PAGE_SIZE * (++page_size_counter);
		if (page_size > PAGE_SIZE*MAX_PAGE_COUNT)
			break;
		block_count = ((page_size - sizeof(chunk_t)) / (block_size + 1));
		if (block_count > 255)
			break;
		if (!block_count)
			++block_count;
		header_size = _memory_chunk_header_size(block_count);
		if (header_size + (block_size * block_count) > page_size)
			--block_count;
		wasted = (page_size - header_size) - (block_size * block_count);
		overhead = wasted + header_size;

		current_factor = (float)overhead / ((float)block_count * (float)block_size);
		if (current_factor < best_factor) {
			best_factor = current_factor;
			best_page_count = page_size_counter;
			best_wasted = wasted;
			best_overhead = overhead;
			best_block_count = block_count;
		}
	}
	log_memory_spamf("Size class %" PRIsize ": %" PRIsize " pages, %" PRIsize " bytes, %" PRIsize
	                 " blocks of %" PRIsize " bytes (%" PRIsize " wasted, %" PRIsize " overhead, %.2f%%)",
	                 iclass, best_page_count, PAGE_SIZE * best_page_count, best_block_count, block_size, best_wasted,
	                 best_overhead, 100.0f * best_factor);

	heap->size_class[iclass].page_count = (uint16_t)best_page_count;
	heap->size_class[iclass].block_count = (uint16_t)best_block_count;

	//Check if previous size class can be merged
	if ((iclass > 0) &&
	        (heap->size_class[iclass-1].page_count == heap->size_class[iclass].page_count) &&
	        (heap->size_class[iclass-1].block_count == heap->size_class[iclass].block_count)) {
		log_memory_spam("  merge previous size class");
		heap->size_class[iclass-1].size = 0;
	}
}

static heap_t*
_memory_allocate_heap(void) {
	size_t iclass;
	heap_t* heap = _memory_map(1);
	memset(heap, 0, sizeof(heap_t));

	for (iclass = 0; iclass < SMALL_CLASS_COUNT; ++iclass) {
		size_t size = (iclass + 1) * SMALL_GRANULARITY;
		heap->size_class[iclass].size = (uint16_t)size;

		_memory_adjust_size_class(heap, iclass);
	}

	for (iclass = 0; iclass < MEDIUM_CLASS_COUNT; ++iclass) {
		size_t size = SMALL_SIZE_LIMIT + ((iclass + 1) * MEDIUM_SIZE_INCR);
		heap->size_class[SMALL_CLASS_COUNT + iclass].size = (uint16_t)size;

		_memory_adjust_size_class(heap, SMALL_CLASS_COUNT + iclass);
	}

	heap->id = (uint16_t)atomic_incr32(&_memory_heap_id);

	if (_memory_heaps_lock)
		mutex_lock(_memory_heaps_lock);

	size_t list_idx = heap->id % HEAP_ARRAY_SIZE;
	heap->next_heap = _memory_heaps[list_idx];
	_memory_heaps[list_idx] = heap;

	if (_memory_heaps_lock)
		mutex_unlock(_memory_heaps_lock);

	return heap;
}

static void
_memory_deallocate_heap(heap_t* heap) {
	//unlink heap from heap lists arrays
	if (_memory_heaps_lock)
		mutex_lock(_memory_heaps_lock);

	size_t list_idx = heap->id % HEAP_ARRAY_SIZE;
	if (_memory_heaps[list_idx] == heap) {
		_memory_heaps[list_idx] = heap->next_heap;
	}
	else {
		heap_t* prev = _memory_heaps[list_idx];
		while (prev && (prev->next_heap != heap))
			prev = prev->next_heap;
		if (prev)
			prev->next_heap = heap->next_heap;
	}

	if (_memory_heaps_lock)
		mutex_unlock(_memory_heaps_lock);

	for (size_t ispan = 0; ispan < SPAN_CLASS_COUNT; ++ispan) {
		span_t* span;
		span_t* next_span;

		//Return all possible thread cache spans to global cache
		while (heap->span_cache[ispan] && (heap->span_cache[ispan]->total_span_count >= MINIMUM_NUMBER_OF_THREAD_CACHE_SPANS)) {
			next_span = _memory_global_cache_insert(heap->span_cache[ispan], ispan);
			if (next_span == heap->span_cache[ispan])
				break;
			heap->span_cache[ispan] = next_span;
		}

		//Release remaining thread cache spans
		for (span = heap->span_cache[ispan]; span; span = next_span) {
			next_span = span->next_span;
			_memory_unmap(span, ispan);
		}
	}

	_memory_unmap(heap, 1);
}

static void*
_memory_allocate(hash_t context, size_t size, unsigned int align, unsigned int hint) {
	//Everything is 16 byte aligned
	FOUNDATION_UNUSED(context);
	FOUNDATION_UNUSED(align);

	if (size > MEDIUM_SIZE_LIMIT) {
		size += CHUNK_HEADER_SIZE;
		size_t num_pages = size / PAGE_SIZE;
		if (size % PAGE_SIZE)
			++num_pages;
		chunk_t* chunk = _memory_allocate_raw(0, num_pages);
		chunk->size_class = 0xFF;
		size_t* chunk_page_count = (size_t*)((void*)chunk);
		*chunk_page_count = num_pages;
		return pointer_offset(chunk, CHUNK_HEADER_SIZE);
	}

	//TODO: Add likely/unlikely branch hints in foundation lib
	heap_t* heap = get_thread_heap();
	if (!heap) {
		heap = _memory_allocate_heap();
		if (!heap)
			return 0;
		set_thread_heap(heap);
	}

	_memory_deallocate_delegated(heap);

	void* block = _memory_allocate_from_heap(heap, size);
	if (block && (hint & MEMORY_ZERO_INITIALIZED))
		memset(block, 0, size);

	return block;
}

static void
_memory_deallocate(void* p) {
	if (!p)
		return;

	chunk_t* chunk = (void*)((uintptr_t)p & (uintptr_t)PAGE_MASK);
	if (chunk->size_class == 0xFF) {
		size_t* chunk_page_count = (size_t*)((void*)chunk);
		_memory_deallocate_raw(0, chunk, *chunk_page_count);
		return;
	}

	heap_t* heap = get_thread_heap();
	if (heap && (chunk->heap_id == heap->id)) {		
		_memory_deallocate_from_heap(heap, chunk, p);
	}
	else {
		_memory_deallocate_from_other_heap(heap, chunk, p);
	}

	if (heap)
		_memory_deallocate_delegated(heap);
}

static void*
_memory_reallocate(void* p, size_t size, unsigned int align, size_t oldsize) {
	heap_t* heap = get_thread_heap();

	FOUNDATION_UNUSED(align);

	chunk_t* chunk = 0;
	if (p) {
		chunk = (void*)((uintptr_t)p & (uintptr_t)PAGE_MASK);
		if (chunk->size_class != 0xFF) {
			//All heaps have the same size_class setup, safe to use this even if chunk
			//is allocated from some other heap
			if (heap) {
				size_class_t* size_class = heap->size_class + chunk->size_class;
				if (size_class->size >= size)
					return p; //Still fits in block
			}
		}
	}

	void* block = _memory_allocate(0, size, align, 0);
	if (p) {
		memcpy(block, p, oldsize < size ? oldsize : size);
		_memory_deallocate(p);
	}

	return block;
}


static int
_memory_initialize(void) {
#if FOUNDATION_PLATFORM_WINDOWS
	/*NtAllocateVirtualMemory = (NtAllocateVirtualMemoryFn)GetProcAddress(GetModuleHandleA("ntdll.dll"),
	                          "NtAllocateVirtualMemory");
	if (!NtAllocateVirtualMemory)
		return -1; */
	SYSTEM_INFO system_info;
	memset(&system_info, 0, sizeof(system_info));
	GetSystemInfo(&system_info);
	if (system_info.dwAllocationGranularity < 0x1000)
		return -1;
#endif
#if FOUNDATION_PLATFORM_POSIX
	atomic_store64(&_memory_addr, 0x1000000000ULL);
#endif

	_memory_heaps_lock = mutex_allocate(STRING_CONST("memory-heaps"));

	return 0;
}

static void
_memory_finalize(void) {
	mutex_deallocate(_memory_heaps_lock);

	for (size_t list_idx = 0; list_idx < HEAP_ARRAY_SIZE; ++list_idx) {
		FOUNDATION_ASSERT(!_memory_heaps[list_idx]);
	}

	FOUNDATION_ASSERT(!atomic_load_ptr(&_memory_delegated_deallocate));
}

static void
_memory_thread_finalize(void) {
	heap_t* heap = get_thread_heap();
	if (!heap)
		return;
	_memory_deallocate_heap(heap);
}

memory_system_t
memory_system(void) {
	memory_system_t memsystem;
	memsystem.allocate = _memory_allocate;
	memsystem.reallocate = _memory_reallocate;
	memsystem.deallocate = _memory_deallocate;
	memsystem.initialize = _memory_initialize;
	memsystem.finalize = _memory_finalize;
	memsystem.thread_finalize = _memory_thread_finalize;
	return memsystem;
}

memory_statistics_detail_t
memory_statistics_detailed(void) {
	memory_statistics_detail_t memory_statistics;
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	memcpy(&memory_statistics, &_memory_statistics, sizeof(memory_statistics));
#else
	memset(&memory_statistics, 0, sizeof(memory_statistics));
#endif
	return memory_statistics;
}

