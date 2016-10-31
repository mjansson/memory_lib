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

//#define MEMORY_ASSERT       FOUNDATION_ASSERT
#define MEMORY_ASSERT(...)

#define PAGE_SIZE           4096
#define MAX_CHUNK_SIZE      65536
#define MAX_PAGE_COUNT      (MAX_CHUNK_SIZE/PAGE_SIZE)
#define PAGE_MASK           (~(MAX_CHUNK_SIZE - 1))

#define CHUNK_HEADER_SIZE   16

#define SMALL_GRANULARITY   16
#define SMALL_CLASS_COUNT   (((PAGE_SIZE - CHUNK_HEADER_SIZE) / 2) / SMALL_GRANULARITY)
#define SMALL_SIZE_LIMIT    (SMALL_CLASS_COUNT * SMALL_GRANULARITY)

#define MEDIUM_SIZE_LIMIT   (MAX_CHUNK_SIZE - CHUNK_HEADER_SIZE)
#define MEDIUM_CLASS_COUNT  32

#define SIZE_CLASS_COUNT    (SMALL_CLASS_COUNT + MEDIUM_CLASS_COUNT)

#define SPAN_CLASS_COUNT    (MAX_CHUNK_SIZE / PAGE_SIZE)

#define pointer_offset_pages(ptr, offset) (pointer_offset((ptr), (uintptr_t)(offset) * PAGE_SIZE))
#define pointer_diff_pages(a, b) ((int32_t)(pointer_diff((a), (b)) / PAGE_SIZE))

typedef struct size_class_t size_class_t;
typedef struct tag_t tag_t;
typedef struct chunk_t chunk_t;
typedef struct heap_t heap_t;
typedef struct span_t span_t;

/* ASSUMPTIONS
   ===========
   Chunk offsets as signed offset in number of pages
   (actual offset is PAGE_SIZE * next_chunk). This assumes
   the maximum distance in memory address space between
   two different pages is less than 2^44

   Assumes all virtual memory pages allocated are on a
   PAGE_SIZE*MAX_PAGE_COUNT alignment, meaning PAGE_MASK-1
   bits are always zero. */

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
	//! Medium block size increments
	size_t medium_block_increment;
	//! Heap ID
	uint16_t id;
};

//! Span of pages
struct span_t {
	//! Next span
	span_t* next_span;
};

FOUNDATION_STATIC_ASSERT(sizeof(heap_t) <= PAGE_SIZE, "Invalid heap size");

FOUNDATION_DECLARE_THREAD_LOCAL(heap_t*, heap, 0);

static atomic32_t _memory_heap_id;
static atomicptr_t _memory_span_cache[SPAN_CLASS_COUNT];

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

static void*
_memory_allocate_raw(size_t page_count) {
	//Check if we can find a span that matches the request
	if (page_count < SPAN_CLASS_COUNT) {
		span_t* span;
		atomic_thread_fence_acquire();
		do {
			span = atomic_load_ptr(&_memory_span_cache[page_count]);
			if (!span)
				break;
		}
		while (!atomic_cas_ptr(&_memory_span_cache[page_count], span->next_span, span));

		if (span)
			return span;
	}

	void* pages_ptr = 0;

#if FOUNDATION_PLATFORM_WINDOWS
	pages_ptr = VirtualAlloc(0, PAGE_SIZE * page_count, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	MEMORY_ASSERT(!((uintptr_t)pages_ptr & 0xFFFF));

	/* For controlling high order bits
	long vmres = NtAllocateVirtualMemory(((HANDLE)(LONG_PTR)-1), &pages_ptr, 1, &allocate_size,
	                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	*/
#endif

	return pages_ptr;
}

static void
_memory_deallocate_raw(void* ptr, size_t page_count) {
	if (page_count < SPAN_CLASS_COUNT) {
		//Insert into global span cache
		span_t* span = ptr;
		atomic_thread_fence_acquire();
		do {
			span->next_span = atomic_load_ptr(&_memory_span_cache[page_count]);
		}
		while (!atomic_cas_ptr(&_memory_span_cache[page_count], span, span->next_span));

		//TODO: Free spans if total amount of memory for class > threshold

		return;
	}

#if FOUNDATION_PLATFORM_WINDOWS
	VirtualFree(ptr, 0, MEM_RELEASE);
#endif
}

static void
_memory_adjust_size_class(heap_t* heap, size_t iclass) {
	size_t block_size = heap->size_class[iclass].size;
	size_t block_count = ((PAGE_SIZE - sizeof(chunk_t)) / (block_size + 1));
	size_t header_size = _memory_chunk_header_size(block_count);
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
		log_memory_spamf("  merge previous size class");
		heap->size_class[iclass-1].size = 0;
	}
}

static heap_t*
_memory_allocate_heap(void) {
	size_t iclass;
	heap_t* heap = _memory_allocate_raw(1);

	for (iclass = 0; iclass < SMALL_CLASS_COUNT; ++iclass) {
		size_t size = (iclass + 1) * SMALL_GRANULARITY;
		heap->size_class[iclass].size = (uint16_t)size;
		heap->size_class[iclass].free_chunk_list = 0;
		heap->size_class[iclass].full_chunk_list = 0;
		heap->size_class[iclass].free_count = 0;

		_memory_adjust_size_class(heap, iclass);
	}

	size_t increment = (MAX_CHUNK_SIZE - CHUNK_HEADER_SIZE - SMALL_SIZE_LIMIT) /
	                   MEDIUM_CLASS_COUNT;
	if (increment % 16)
		increment += (16 - (increment % 16));

	heap->medium_block_increment = increment;

	for (iclass = 0; iclass < MEDIUM_CLASS_COUNT; ++iclass) {
		size_t size = SMALL_SIZE_LIMIT + (iclass + 1) * increment;
		heap->size_class[SMALL_CLASS_COUNT + iclass].size = (uint16_t)size;
		heap->size_class[SMALL_CLASS_COUNT + iclass].free_chunk_list = 0;
		heap->size_class[SMALL_CLASS_COUNT + iclass].full_chunk_list = 0;
		heap->size_class[SMALL_CLASS_COUNT + iclass].free_count = 0;

		_memory_adjust_size_class(heap, SMALL_CLASS_COUNT + iclass);
	}

	heap->id = (uint16_t)atomic_incr32(&_memory_heap_id);

	return heap;
}

static void*
_memory_allocate_from_heap(heap_t* heap, size_t size) {
	size_t class_idx = (size ? (size-1) : 0) / SMALL_GRANULARITY;
	if (class_idx >= SMALL_CLASS_COUNT) {
		MEMORY_ASSERT(size > SMALL_SIZE_LIMIT);
		class_idx = SMALL_CLASS_COUNT + ((size - SMALL_SIZE_LIMIT) / heap->medium_block_increment);
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
		chunk = _memory_allocate_raw(size_class->page_count);
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
		if (size_class->free_chunk_list == (int32_t)pointer_diff_pages(chunk, heap)) {
			if (chunk->next_chunk) {
				chunk_t* next_chunk = (chunk_t*)pointer_offset_pages(chunk, chunk->next_chunk);
				size_class->free_chunk_list = (int32_t)pointer_diff_pages(next_chunk, heap);
				MEMORY_ASSERT(pointer_offset_pages(heap, size_class->free_chunk_list) == next_chunk);
			}
			else {
				size_class->free_chunk_list = 0;
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

		if (size_class->full_chunk_list) {
			chunk_t* next_chunk = pointer_offset_pages(heap, size_class->full_chunk_list);
			chunk->next_chunk = (int32_t)pointer_diff_pages(next_chunk, chunk);
			next_chunk->prev_chunk = (int32_t)pointer_diff_pages(chunk, next_chunk);
		}
		else {
			chunk->next_chunk = 0;
		}
		size_class->full_chunk_list = (int32_t)pointer_diff_pages(chunk, heap);

		MEMORY_ASSERT(size_class->full_chunk_list != size_class->free_chunk_list);
	}
	else {
		MEMORY_ASSERT(next_offset);
		chunk->free_list = (uint8_t)(block_idx + next_offset);
	}

	log_memory_spamf("Allocated block %d of %u, new first free block is %u (remains %u)\n",
	                 block_idx + 1, (unsigned int)chunk->block_count, (unsigned int)chunk->free_list,
	                 (unsigned int)chunk->free_count);

	size_t offset = header_size + (block_idx * size_class->size);
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
	size_t block_idx = pointer_diff(p, block_start) / size_class->size;

	bool was_full = !chunk->free_count;

	if (was_full) {
		//Remove chunk from list of full chunks
		if (size_class->full_chunk_list == (int32_t)pointer_diff_pages(chunk, heap)) {
			if (chunk->next_chunk) {
				chunk_t* next_chunk = (chunk_t*)pointer_offset_pages(chunk, chunk->next_chunk);
				size_class->full_chunk_list = (int32_t)pointer_diff_pages(next_chunk, heap);
			}
			else {
				size_class->full_chunk_list = 0;
			}
		}
		else {
			//If this chunk was not first in full_chunk_list, it must have a previous chunk in list
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
	}

	++size_class->free_count;
	++chunk->free_count;
	if ((chunk->free_count == chunk->block_count) && size_class->free_chunk_list &&
	        (size_class->free_count > (size_class->block_count * 2)) &&
	        (size_class->free_count > 8)) {
		//Chunk went from full or partial free -> free
		//Check if there is another chunk with available blocks, if so return pages
		//From free_chunk_list check above we know there is at least one chunk at least partial free
		MEMORY_ASSERT(was_full || (size_class->free_chunk_list != chunk_offset) || chunk->next_chunk);
		int32_t chunk_offset = (int32_t)pointer_diff_pages(chunk, heap);
		if (size_class->free_chunk_list == chunk_offset) {
			MEMORY_ASSERT(chunk->next_chunk);
			chunk_t* next_chunk = (chunk_t*)pointer_offset_pages(chunk, chunk->next_chunk);
			size_class->free_chunk_list = (int32_t)pointer_diff_pages(next_chunk, heap);
			MEMORY_ASSERT(pointer_offset_pages(heap, size_class->free_chunk_list) == next_chunk);
		}
		else if (!was_full) {
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

		MEMORY_ASSERT(size_class->full_chunk_list != size_class->free_chunk_list);

		size_class->free_count -= size_class->block_count;

		_memory_deallocate_raw(chunk, size_class->page_count);

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
	if (size_class->free_chunk_list) {
		chunk_t* next_chunk = pointer_offset_pages(heap, size_class->free_chunk_list);
		chunk->next_chunk = (int32_t)pointer_diff_pages(next_chunk, chunk);
		next_chunk->prev_chunk = (int32_t)pointer_diff_pages(chunk, next_chunk);
	}
	else {
		chunk->next_chunk = 0;
	}
	size_class->free_chunk_list = (int32_t)pointer_diff_pages(chunk, heap);
	MEMORY_ASSERT(pointer_offset_pages(heap, size_class->free_chunk_list) == chunk);

	MEMORY_ASSERT(size_class->full_chunk_list != size_class->free_chunk_list);
}

static int
_memory_initialize(void) {
#if FOUNDATION_PLATFORM_WINDOWS
	NtAllocateVirtualMemory = (NtAllocateVirtualMemoryFn)GetProcAddress(GetModuleHandleA("ntdll.dll"),
	                          "NtAllocateVirtualMemory");
#endif
	return 0;
}

static void
_memory_finalize(void) {
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
		chunk_t* chunk = _memory_allocate_raw(num_pages);
		chunk->size_class = 0xFF;
		size_t* chunk_page_count = (size_t*)chunk->next_block;
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

	void* block = _memory_allocate_from_heap(heap, size);
	if (block && (hint & MEMORY_ZERO_INITIALIZED))
		memset(block, 0, size);

	return block;
}

static void*
_memory_reallocate(void* p, uint64_t size, unsigned int align, uint64_t oldsize) {
	heap_t* heap = get_thread_heap();

	chunk_t* chunk;
	if (p) {
		chunk = (void*)((uintptr_t)p & PAGE_MASK);
		if (chunk->size_class != 0xFF) {
			if (heap && (chunk->heap_id == heap->id)) {
				size_class_t* size_class = heap->size_class + chunk->size_class;
				if (size_class->size >= size)
					return p; //Still fits in block
			}
		}
	}

	if (!heap) {
		heap = _memory_allocate_heap();
		if (!heap)
			return 0;
		set_thread_heap(heap);
	}

	void* block = _memory_allocate_from_heap(heap, size);
	if (p) {
		memcpy(block, p, oldsize < size ? oldsize : size);
		_memory_deallocate_from_heap(heap, chunk, p);
	}

	return block;
}

static void
_memory_deallocate(void* p) {
	if (!p)
		return;

	chunk_t* chunk = (void*)((uintptr_t)p & PAGE_MASK);
	if (chunk->size_class == 0xFF) {
		size_t* chunk_page_count = (size_t*)chunk->next_block;
		_memory_deallocate_raw(chunk, *chunk_page_count);
		return;
	}

	heap_t* heap = get_thread_heap();
	if (!heap || (chunk->heap_id != heap->id)) {
		//Delegate to correct thread, or adopt if orphaned and we have a heap
		//If neither, delegate to first thread we can find
		FOUNDATION_ASSERT(false);
		return;
	}

	_memory_deallocate_from_heap(heap, chunk, p);
}



memory_system_t
memory_system(void) {
	memory_system_t memsystem;
	memsystem.allocate = _memory_allocate;
	memsystem.reallocate = _memory_reallocate;
	memsystem.deallocate = _memory_deallocate;
	memsystem.initialize = _memory_initialize;
	memsystem.finalize = _memory_finalize;
	return memsystem;
}
