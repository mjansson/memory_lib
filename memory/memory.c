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

#if FOUNDATION_PLATFORM_POSIX
#  include <sys/mman.h>
#  ifndef MAP_UNINITIALIZED
#    define MAP_UNINITIALIZED 0
#  endif
#  ifndef MAP_ANONYMOUS
#    define MAP_ANONYMOUS MAP_ANON
#  endif
#endif

//#define MEMORY_ASSERT             FOUNDATION_ASSERT
#define MEMORY_ASSERT(...)

#define PAGE_SIZE                 4096

#define SPAN_ADDRESS_GRANULARITY  65536
#define SPAN_MAX_SIZE             (SPAN_ADDRESS_GRANULARITY)
#define SPAN_MASK                 (~(SPAN_MAX_SIZE - 1))
#define SPAN_MAX_PAGE_COUNT       (SPAN_MAX_SIZE / PAGE_SIZE)
#define SPAN_CLASS_COUNT          SPAN_MAX_PAGE_COUNT

#define SMALL_GRANULARITY         16
#define SMALL_CLASS_COUNT         (((PAGE_SIZE - SPAN_HEADER_SIZE) / 2) / SMALL_GRANULARITY)
#define SMALL_SIZE_LIMIT          (SMALL_CLASS_COUNT * SMALL_GRANULARITY)

#define MEDIUM_CLASS_COUNT        32
#define MEDIUM_SIZE_INCR_UNALIGN  (((SPAN_MAX_SIZE - SPAN_HEADER_SIZE) - SMALL_SIZE_LIMIT) / MEDIUM_CLASS_COUNT)
#define MEDIUM_SIZE_INCR          (MEDIUM_SIZE_INCR_UNALIGN - (MEDIUM_SIZE_INCR_UNALIGN % 16))
#define MEDIUM_SIZE_LIMIT         (SMALL_SIZE_LIMIT + (MEDIUM_CLASS_COUNT * MEDIUM_SIZE_INCR))

#define SIZE_CLASS_COUNT          (SMALL_CLASS_COUNT + MEDIUM_CLASS_COUNT)

#define BLOCK_AUTOLINK            0x80000000
#define SPAN_LIST_LOCK_TOKEN      ((void*)1)

#define THREAD_SPAN_CACHE_LIMIT   32
#define GLOBAL_SPAN_CACHE_LIMIT   (THREAD_SPAN_CACHE_LIMIT*128)
#define HEAP_ARRAY_SIZE           197

#define pointer_offset_span(ptr, offset) (pointer_offset((ptr), (intptr_t)(offset) * (intptr_t)SPAN_ADDRESS_GRANULARITY))
#define pointer_diff_span(a, b) ((offset_t)((intptr_t)pointer_diff((a), (b)) / (intptr_t)SPAN_ADDRESS_GRANULARITY))

#if (FOUNDATION_SIZE_POINTER > 4) && BUILD_USE_FULL_ADDRESS_RANGE
#define SPAN_HEADER_SIZE          32
typedef int64_t offset_t;
#else
#define SPAN_HEADER_SIZE          16
typedef int32_t offset_t;
#endif

typedef struct span_t span_t;
typedef struct heap_t heap_t;
typedef struct size_class_t size_class_t;
typedef struct memory_statistics_atomic_t memory_statistics_atomic_t;

struct span_t {
	//!	Heap ID
	atomic32_t heap_id;
	//! Size class
	uint8_t    size_class;
	//! Free list
	uint8_t    free_list;
	//! Free count
	uint8_t    free_count;
	//! Cache list size
	uint8_t    list_size;
	//! Next span
	offset_t   next_span;
	//! Previous span
	offset_t   prev_span;
};
FOUNDATION_STATIC_ASSERT(sizeof(span_t) <= SPAN_HEADER_SIZE, "span size mismatch");

struct heap_t {
	//! Heap ID
	int32_t     id;
	//! Next heap
	heap_t*     next_heap;
	//! List of spans with free blocks for each size class
	span_t*     size_cache[SIZE_CLASS_COUNT];
	//! List of free spans for each page count
	span_t*     span_cache[SPAN_CLASS_COUNT];
	//! Deferred deallocation
	atomicptr_t defer_deallocate;
};
FOUNDATION_STATIC_ASSERT(sizeof(heap_t) <= PAGE_SIZE, "heap size mismatch");

struct size_class_t {
	//! Size of blocks in this class
	uint32_t size;
	//! Number of pages to allocate for a chunk
	uint16_t page_count;
	//! Number of blocks in each chunk
	uint16_t block_count;
};

struct memory_statistics_atomic_t {
	atomic64_t allocations_total;
	atomic64_t allocations_current;
	atomic64_t allocated_total;
	atomic64_t allocated_current;
	atomic64_t allocations_total_virtual;
	atomic64_t allocations_current_virtual;
	atomic64_t allocated_total_virtual;
	atomic64_t allocated_current_virtual;
	atomic64_t thread_cache_hits;
	atomic64_t thread_cache_misses;
};
FOUNDATION_STATIC_ASSERT(sizeof(memory_statistics_detail_t) == sizeof(memory_statistics_atomic_t),
                         "Statistics size mismatch");

//! Global size classes
static size_class_t _memory_size_class[SIZE_CLASS_COUNT];

//! Heap ID counter
static atomic32_t _memory_heap_id;

#if FOUNDATION_PLATFORM_POSIX
//! Virtual memory address counter
static atomic64_t _memory_addr;
#endif

//! Global span cache
static atomicptr_t _memory_span_cache[SPAN_CLASS_COUNT];

//! Current thread heap
FOUNDATION_DECLARE_THREAD_LOCAL(heap_t*, heap, 0)

//! All heaps
static atomicptr_t _memory_heaps[HEAP_ARRAY_SIZE];

//! Orphaned heaps
static atomicptr_t _memory_orphan_heaps;

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
//! Detailed statistics
static memory_statistics_atomic_t _memory_statistics;
#endif

static heap_t*
_memory_heap_lookup(int32_t id) {
	uint32_t list_idx = id % HEAP_ARRAY_SIZE;
	heap_t* heap = atomic_loadptr(&_memory_heaps[list_idx]);
	while (heap && (heap->id != id))
		heap = heap->next_heap;
	return heap;
}

static void*
_memory_map(size_t page_count) {
	size_t total_size = page_count * PAGE_SIZE;
	void* pages_ptr = 0;

#if FOUNDATION_PLATFORM_WINDOWS
	pages_ptr = VirtualAlloc(0, total_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	MEMORY_ASSERT(!((uintptr_t)pages_ptr & ~(uintptr_t)SPAN_MASK));

	/* For controlling high order bits
	long vmres = NtAllocateVirtualMemory(((HANDLE)(LONG_PTR)-1), &pages_ptr, 1, &allocate_size,
	                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	*/
#else
	intptr_t incr = (intptr_t)total_size / (intptr_t)SPAN_ADDRESS_GRANULARITY;
	if (total_size % SPAN_ADDRESS_GRANULARITY)
		++incr;
	do {
		void* base_addr = (void*)(uintptr_t)atomic_exchange_and_add64(&_memory_addr, incr * (intptr_t)SPAN_ADDRESS_GRANULARITY);
		pages_ptr = mmap(base_addr, total_size, PROT_READ | PROT_WRITE,
		                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED, -1, 0);
		if (!((uintptr_t)pages_ptr & ~(uintptr_t)SPAN_MASK)) {
			if (pages_ptr != base_addr) {
				pages_ptr = (void*)((uintptr_t)pages_ptr & (uintptr_t)SPAN_MASK);
				atomic_store64(&_memory_addr, (int64_t)((uintptr_t)pages_ptr) + (intptr_t)SPAN_ADDRESS_GRANULARITY);
				atomic_thread_fence_release();
			}
			break;
		}
		munmap(pages_ptr, total_size);
	}
	while (true);
#endif

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_incr64(&_memory_statistics.allocations_total_virtual);
	atomic_incr64(&_memory_statistics.allocations_current_virtual);
	if (pages_ptr) {
		atomic_add64(&_memory_statistics.allocated_total_virtual, (int64_t)total_size);
		atomic_add64(&_memory_statistics.allocated_current_virtual, (int64_t)total_size);
	}
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
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_add64(&_memory_statistics.allocated_current_virtual, (int64_t)PAGE_SIZE * -(int64_t)page_count);
	atomic_decr64(&_memory_statistics.allocations_current_virtual);
#endif
}

static void
_memory_global_cache_insert(span_t* first_span, size_t page_count, size_t span_count) {
	while (true) {
		void* global_span_ptr = atomic_load_ptr(&_memory_span_cache[page_count-1]);
		if (global_span_ptr != SPAN_LIST_LOCK_TOKEN) {
			uintptr_t global_span_count = (uintptr_t)global_span_ptr & ~(uintptr_t)SPAN_MASK;
			span_t* global_span = (span_t*)((void*)((uintptr_t)global_span_ptr & (uintptr_t)SPAN_MASK));

#if GLOBAL_SPAN_CACHE_LIMIT > 0
			if (global_span_count >= GLOBAL_SPAN_CACHE_LIMIT)
				break;
#endif

			//Use prev_span as skip pointer over this range of spans
			first_span->list_size = (uint8_t)span_count;
			first_span->prev_span = global_span ? pointer_diff_span(global_span, first_span) : 0;

			global_span_count += span_count;
			void* first_span_ptr = (void*)((uintptr_t)first_span | global_span_count);

			if (atomic_cas_ptr(&_memory_span_cache[page_count-1], first_span_ptr, global_span_ptr)) {
				//Span list is inserted into global cache
				return;
			}
		}
		else {
			thread_yield();
			atomic_thread_fence_acquire();
		}
	}

#if GLOBAL_SPAN_CACHE_LIMIT > 0
	//Global cache full, release pages
	for (size_t ispan = 0; ispan < span_count; ++ispan) {
		span_t* next_span = pointer_offset_span(first_span, first_span->next_span);
		_memory_unmap(first_span, page_count);
		first_span = next_span;
	}
#endif
}

static span_t*
_memory_global_cache_extract(size_t page_count) {
	span_t* span = 0;
	atomicptr_t* cache = &_memory_span_cache[page_count-1];
	atomic_thread_fence_acquire();
	void* global_span_ptr = atomic_load_ptr(cache);
	while (global_span_ptr) {
		if ((global_span_ptr != SPAN_LIST_LOCK_TOKEN) &&
		        atomic_cas_ptr(cache, SPAN_LIST_LOCK_TOKEN, global_span_ptr)) {
			//Grab a number of thread cache spans, using the skip span pointer stored in prev_span to quickly
			//skip ahead in the list to get the new head
			uintptr_t global_span_count = (uintptr_t)global_span_ptr & ~(uintptr_t)SPAN_MASK;
			span = (span_t*)((void*)((uintptr_t)global_span_ptr & (uintptr_t)SPAN_MASK));

			span_t* new_global_span = pointer_offset_span(span, span->prev_span);
			global_span_count -= span->list_size;

			void* new_cache_head = global_span_count ?
			                       ((void*)((uintptr_t)new_global_span | global_span_count)) :
			                       0;

			atomic_store_ptr(cache, new_cache_head);
			atomic_thread_fence_release();

			break;
		}

		thread_yield();
		atomic_thread_fence_acquire();

		global_span_ptr = atomic_load_ptr(cache);
	}

	return span;
}

static void*
_memory_allocate_from_heap(heap_t* heap, size_t size) {
	size_t class_idx = (size ? (size-1) : 0) / SMALL_GRANULARITY;
	if (class_idx >= SMALL_CLASS_COUNT) {
		MEMORY_ASSERT(size > SMALL_SIZE_LIMIT);
		class_idx = SMALL_CLASS_COUNT + ((size - (1 + SMALL_SIZE_LIMIT)) / MEDIUM_SIZE_INCR);
	}

	size_class_t* size_class = _memory_size_class + class_idx;
	while (size_class->size < size) {
		++size_class;
		++class_idx;
	}
	MEMORY_ASSERT(class_idx < SIZE_CLASS_COUNT);

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_add64(&_memory_statistics.allocated_total, (int64_t)size_class->size);
	atomic_add64(&_memory_statistics.allocated_current, (int64_t)size_class->size);
#endif

	span_t* span = heap->size_cache[class_idx];
	if (FOUNDATION_LIKELY(span)) {
		//Happy path, we have a span with at least one free block
		size_t offset = size_class->size * span->free_list;
		uint32_t* block = pointer_offset(span, SPAN_HEADER_SIZE + offset);
		if (!(*block & BLOCK_AUTOLINK)) {
			span->free_list = (uint8_t)(*block);
		}
		else {
			++span->free_list;
		}
		MEMORY_ASSERT(span->free_count > 0);
		--span->free_count;
		if (!span->free_count) {
			span_t* next_span = span->next_span ? pointer_offset_span(span, span->next_span) : 0;
			heap->size_cache[class_idx] = next_span;
		}
		else {
			MEMORY_ASSERT(span->free_list < size_class->block_count);
			if (*block & BLOCK_AUTOLINK) {
				size_t next_offset = size_class->size * span->free_list;
				uint32_t* next_block = pointer_offset(span, SPAN_HEADER_SIZE + next_offset);
				*next_block = BLOCK_AUTOLINK;
			}
		}
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
		atomic_incr64(&_memory_statistics.thread_cache_hits);
#endif
		return block;
	}

	//No span in use, grab a new span from heap cache
	span = heap->span_cache[size_class->page_count-1];
	if (FOUNDATION_LIKELY(span)) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
		atomic_incr64(&_memory_statistics.thread_cache_hits);
#endif
	}
	else {
		span = _memory_global_cache_extract(size_class->page_count);
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
		atomic_incr64(&_memory_statistics.thread_cache_misses);
#endif
	}
	if (FOUNDATION_LIKELY(span)) {
		if (span->list_size > 1) {
			MEMORY_ASSERT(span->next_span);
			span_t* next_span = pointer_offset_span(span, span->next_span);
			next_span->list_size = span->list_size - 1;
			heap->span_cache[size_class->page_count-1] = next_span;
		}
		else {
			heap->span_cache[size_class->page_count-1] = 0;
		}
	}
	else {
		span = _memory_map(size_class->page_count);
	}

	atomic_store32(&span->heap_id, heap->id);
	atomic_thread_fence_release();

	span->size_class = (uint8_t)class_idx;
	span->free_count = (uint8_t)(size_class->block_count - 1);
	span->next_span = 0;

	//If we only have one block we will grab it, otherwise
	//set span as new span to use for next allocation
	if (size_class->block_count > 1) {
		span->free_list = 1;

		uint32_t* next_block = pointer_offset(span, SPAN_HEADER_SIZE + size_class->size);
		*next_block = BLOCK_AUTOLINK;

		MEMORY_ASSERT(!heap->size_cache[class_idx]);
		heap->size_cache[class_idx] = span;
	}

	//Return first block
	return pointer_offset(span, SPAN_HEADER_SIZE);
}

static heap_t*
_memory_allocate_heap(void) {
	heap_t* heap;
	heap_t* next_heap;
	atomic_thread_fence_acquire();
	do {
		heap = atomic_load_ptr(&_memory_orphan_heaps);
		if (!heap)
			break;
		next_heap = heap->next_heap;
	} while (!atomic_cas_ptr(&_memory_orphan_heaps, next_heap, heap));

	if (heap)
		return heap;

	heap = _memory_map(1);
	memset(heap, 0, sizeof(heap_t));

	do {
		heap->id = atomic_incr32(&_memory_heap_id);
		if (_memory_heap_lookup(heap->id))
			heap->id = 0;
	}
	while (!heap->id);

	size_t list_idx = heap->id % HEAP_ARRAY_SIZE;
	do {
		next_heap = atomic_loadptr(&_memory_heaps[list_idx]);
		heap->next_heap = next_heap;
	} while (!atomic_cas_ptr(&_memory_heaps[list_idx], heap, next_heap));

	return heap;
}

static void
_memory_list_add(span_t** head, span_t* span) {
	if (*head) {
		offset_t next_offset = pointer_diff_span(*head, span);
		(*head)->prev_span = -next_offset;
		span->next_span = next_offset;
		span->list_size = (*head)->list_size + 1;
	}
	else {
		span->next_span = 0;
		span->list_size = 1;
	}
	*head = span;
}

static void
_memory_list_remove(span_t** head, span_t* span) {
	if (*head == span) {
		if (span->next_span) {
			*head = pointer_offset_span(span, span->next_span);
			(*head)->list_size = span->list_size - 1;
		}
		else {
			*head = 0;
		}
	}
	else {
		span_t* prev_span = pointer_offset_span(span, span->prev_span);
		if (span->next_span) {
			span_t* next_span = pointer_offset_span(span, span->next_span);
			offset_t next_offset = pointer_diff_span(next_span, prev_span);
			prev_span->next_span = next_offset;
			next_span->prev_span = -next_offset;
		}
		else {
			prev_span->next_span = 0;
		}
		--(*head)->list_size;
	}
}

static void
_memory_deallocate_to_heap(heap_t* heap, span_t* span, void* p) {
	size_class_t* size_class = _memory_size_class + span->size_class;
	uint32_t* block = p;
	intptr_t block_offset = pointer_diff(block, span) - (intptr_t)SPAN_HEADER_SIZE;
	uint8_t block_idx = (uint8_t)(block_offset / (intptr_t)size_class->size);

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_decr64(&_memory_statistics.allocations_current);
	atomic_add64(&_memory_statistics.allocated_current, -(int64_t)size_class->size);
#endif

	++span->free_count;
	*block = span->free_list;
	span->free_list = block_idx;

	if (span->free_count == size_class->block_count) {
		//Remove from free list (present if we had a previous free block)
		if (span->free_count > 1)
			_memory_list_remove(&heap->size_cache[span->size_class], span);

		//Add to span cache
		span_t** cache = &heap->span_cache[size_class->page_count-1];
		_memory_list_add(cache, span);
		MEMORY_ASSERT((span->list_size == 1) || span->next_span);
		if ((*cache)->list_size > THREAD_SPAN_CACHE_LIMIT) {
			//Release half to global cache
			uint32_t span_count = (THREAD_SPAN_CACHE_LIMIT/2);
			span_t* first_span = *cache;
			span_t* next_span = first_span;

			for (uint32_t ispan = 0; ispan < span_count; ++ispan) {
				MEMORY_ASSERT(next_span->next_span);
				next_span = pointer_offset_span(next_span, next_span->next_span);
			}

			next_span->list_size = first_span->list_size - (uint8_t)span_count;
			*cache = next_span;

			_memory_global_cache_insert(first_span, size_class->page_count, span_count);
		}
	}
	else if (span->free_count == 1) {
		//Add to free list
		_memory_list_add(&heap->size_cache[span->size_class], span);
	}
}

static void
_memory_deallocate_deferred(heap_t* heap) {
	atomic_thread_fence_acquire();
	void* p = atomic_load_ptr(&heap->defer_deallocate);
	if (p && atomic_cas_ptr(&heap->defer_deallocate, 0, p)) {
		while (p) {
			void* next = *(void**)p;
			span_t* span = (void*)((uintptr_t)p & (uintptr_t)SPAN_MASK);
			_memory_deallocate_to_heap(heap, span, p);
			p = next;
		}
	}
}

static void
_memory_deallocate_defer(int32_t heap_id, void* p) {
	//Delegate to heap
	heap_t* heap = _memory_heap_lookup(heap_id);
	MEMORY_ASSERT(heap);
	void* last_ptr;
	do {
		last_ptr = atomic_load_ptr(&heap->defer_deallocate);
		*(void**)p = last_ptr;
	}
	while (!atomic_cas_ptr(&heap->defer_deallocate, p, last_ptr));
}

static void*
_memory_allocate(hash_t context, size_t size, unsigned int align, unsigned int hint) {
	//Everything is already 16 byte aligned
	FOUNDATION_UNUSED(context);
	FOUNDATION_UNUSED(align);

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_incr64(&_memory_statistics.allocations_total);
	atomic_incr64(&_memory_statistics.allocations_current);
#endif

	if (FOUNDATION_UNLIKELY(size > MEDIUM_SIZE_LIMIT)) {
		size += SPAN_HEADER_SIZE;
		size_t num_pages = size / PAGE_SIZE;
		if (size % PAGE_SIZE)
			++num_pages;
		span_t* span = _memory_map(num_pages);
		span->size_class = 0xFF;
		//Store page count in next_span
		span->next_span = (offset_t)num_pages;
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
		atomic_add64(&_memory_statistics.allocated_total, (int64_t)PAGE_SIZE * (int64_t)num_pages);
		atomic_add64(&_memory_statistics.allocated_current, (int64_t)PAGE_SIZE * (int64_t)num_pages);
#endif
		return pointer_offset(span, SPAN_HEADER_SIZE);
	}

	heap_t* heap = get_thread_heap();
	if (FOUNDATION_UNLIKELY(!heap)) {
		heap = _memory_allocate_heap();
		if (!heap)
			return 0;
		set_thread_heap(heap);
	}

	_memory_deallocate_deferred(heap);

	void* block = _memory_allocate_from_heap(heap, size);
	if (block && (hint & MEMORY_ZERO_INITIALIZED))
		memset(block, 0, size);

	return block;
}

static void
_memory_deallocate(void* p) {
	if (FOUNDATION_UNLIKELY(!p))
		return;

	span_t* span = (void*)((uintptr_t)p & (uintptr_t)SPAN_MASK);
	if (FOUNDATION_UNLIKELY(span->size_class == 0xFF)) {
		//Page count is stored in next_span
		size_t num_pages = (size_t)span->next_span;
		_memory_unmap(span, num_pages);
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
		atomic_decr64(&_memory_statistics.allocations_current);
		atomic_add64(&_memory_statistics.allocated_current, (int64_t)PAGE_SIZE * -(int64_t)num_pages);
#endif
		return;
	}

	heap_t* heap = get_thread_heap();
	int32_t heap_id = atomic_load32(&span->heap_id);

	if (FOUNDATION_LIKELY(heap && (heap_id == heap->id))) {
		_memory_deallocate_to_heap(heap, span, p);
	}
	else {
		_memory_deallocate_defer(heap_id, p);
	}
}

static void*
_memory_reallocate(void* p, size_t size, unsigned int align, size_t oldsize) {
	FOUNDATION_UNUSED(align);

	span_t* span = 0;
	if (FOUNDATION_LIKELY(p)) {
		span = (void*)((uintptr_t)p & (uintptr_t)SPAN_MASK);
		if (FOUNDATION_LIKELY(span->size_class != 0xFF)) {
			size_class_t* size_class = _memory_size_class + span->size_class;
			if (size_class->size >= size)
				return p; //Still fits in block
		}
		else {
			size_t total_size = size + SPAN_HEADER_SIZE;
			size_t num_pages = total_size / PAGE_SIZE;
			if (total_size % PAGE_SIZE)
				++num_pages;
			//Page count is stored in next_span
			size_t current_pages = (size_t)span->next_span;
			if ((current_pages >= num_pages) && (num_pages >= (current_pages / 2)))
				return p; //Still fits and less than half of memory would be freed
		}
	}

	void* block = _memory_allocate(0, size, align, 0);
	if (p) {
		memcpy(block, p, oldsize < size ? oldsize : size);
		_memory_deallocate(p);
	}

	return block;
}

static void
_memory_adjust_size_class(size_t iclass) {
	size_t block_size = _memory_size_class[iclass].size;
	size_t header_size = SPAN_HEADER_SIZE;
	size_t remain_size = PAGE_SIZE - header_size;
	size_t block_count = remain_size / block_size;
	size_t wasted = remain_size - (block_size * block_count);

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
		if (page_size > (PAGE_SIZE * SPAN_MAX_PAGE_COUNT))
			break;
		remain_size = page_size - header_size;
		block_count = remain_size / block_size;
		if (block_count > 255)
			break;
		if (!block_count)
			++block_count;
		wasted = remain_size - (block_size * block_count);
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

	_memory_size_class[iclass].page_count = (uint16_t)best_page_count;
	_memory_size_class[iclass].block_count = (uint16_t)best_block_count;

	//Check if previous size class can be merged
	if (iclass > 0) {
		size_t prevclass = iclass - 1;
		if ((_memory_size_class[prevclass].page_count == _memory_size_class[iclass].page_count) &&
		        (_memory_size_class[prevclass].block_count == _memory_size_class[iclass].block_count)) {
			_memory_size_class[prevclass].size = 0;
		}
	}
}

static int
_memory_initialize(void) {
#if FOUNDATION_PLATFORM_WINDOWS
	SYSTEM_INFO system_info;
	memset(&system_info, 0, sizeof(system_info));
	GetSystemInfo(&system_info);
	if (system_info.dwAllocationGranularity < SPAN_ADDRESS_GRANULARITY)
		return -1;
#endif
#if FOUNDATION_PLATFORM_POSIX
	atomic_store64(&_memory_addr, 0x1000000000ULL);
#endif

	atomic_store32(&_memory_heap_id, 0);

	size_t iclass;
	for (iclass = 0; iclass < SMALL_CLASS_COUNT; ++iclass) {
		size_t size = (iclass + 1) * SMALL_GRANULARITY;
		_memory_size_class[iclass].size = (uint16_t)size;
		_memory_adjust_size_class(iclass);
	}
	for (iclass = 0; iclass < MEDIUM_CLASS_COUNT; ++iclass) {
		size_t size = SMALL_SIZE_LIMIT + ((iclass + 1) * MEDIUM_SIZE_INCR);
		_memory_size_class[SMALL_CLASS_COUNT + iclass].size = (uint16_t)size;
		_memory_adjust_size_class(SMALL_CLASS_COUNT + iclass);
	}

	return 0;
}

static void
_memory_finalize(void) {
	atomic_thread_fence_acquire();

	//Free all thread caches
	for (size_t list_idx = 0; list_idx < HEAP_ARRAY_SIZE; ++list_idx) {
		heap_t* heap = atomic_loadptr(&_memory_heaps[list_idx]);
		while (heap) {
			_memory_deallocate_deferred(heap);

			size_t iclass;
			for (iclass = 0; iclass <= SIZE_CLASS_COUNT; ++iclass) {
				MEMORY_ASSERT(!heap->size_cache[iclass]);
			}
			for (iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
				span_t* span = heap->span_cache[iclass];
				unsigned int span_count = span ? span->list_size : 0;
				for (unsigned int ispan = 0; ispan < span_count; ++ispan) {
					span_t* next_span = pointer_offset_span(span, span->next_span);
					_memory_unmap(span, iclass+1);
					span = next_span;
				}
				heap->span_cache[iclass] = 0;
			}
			heap = heap->next_heap;
		}
	}

	//Free global cache
	for (size_t iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
		void* span_ptr = atomic_load_ptr(&_memory_span_cache[iclass]);
		size_t cache_count = (uintptr_t)span_ptr & ~(uintptr_t)SPAN_MASK;
		span_t* span = (span_t*)((void*)((uintptr_t)span_ptr & (uintptr_t)SPAN_MASK));
		while (cache_count) {
			span_t* skip_span = pointer_offset_span(span, span->prev_span);
			unsigned int span_count = span->list_size;
			for (unsigned int ispan = 0; ispan < span_count; ++ispan) {
				span_t* next_span = pointer_offset_span(span, span->next_span);
				_memory_unmap(span, iclass+1);
				span = next_span;
			}
			span = skip_span;
			cache_count -= span_count;
		}
		atomic_store_ptr(&_memory_span_cache[iclass], 0);
	}
}

static void
_memory_thread_finalize(void) {
	heap_t* heap = get_thread_heap();
	if (heap) {
		//Release thread cache spans back to global cache
		for (size_t iclass = 0; iclass < SPAN_CLASS_COUNT; ++iclass) {
			span_t* span = heap->span_cache[iclass];
			unsigned int span_count = span ? span->list_size : 0;
			while (span_count) {
				//Release to global cache
				unsigned int release_count = span_count;
				if (release_count > (THREAD_SPAN_CACHE_LIMIT / 2))
					release_count = THREAD_SPAN_CACHE_LIMIT / 2;

				span_t* first_span = heap->span_cache[iclass];
				span_t* next_span = first_span;

				for (uint32_t ispan = 0; ispan < release_count; ++ispan) {
					next_span = pointer_offset_span(next_span, next_span->next_span);
				}

				heap->span_cache[iclass] = next_span;

				_memory_global_cache_insert(first_span, iclass+1, release_count);

				span_count -= release_count;
			}
			heap->span_cache[iclass] = 0;
		}

		//Orphan the heap
		heap_t* last_heap;
		do {
			last_heap = atomic_load_ptr(&_memory_orphan_heaps);
			heap->next_heap = last_heap;
		} while (!atomic_cas_ptr(&_memory_orphan_heaps, heap, last_heap));
	}
	set_thread_heap(0);
}

memory_statistics_detail_t
memory_statistics_detailed(void) {
	memory_statistics_detail_t memory_statistics;
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_thread_fence_acquire();
	memcpy(&memory_statistics, &_memory_statistics, sizeof(memory_statistics));
#else
	memset(&memory_statistics, 0, sizeof(memory_statistics));
#endif
	return memory_statistics;
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
	memsystem.thread_finalize = _memory_thread_finalize;
	return memsystem;
}
