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

#include <memory/types.h>
#include <memory/log.h>

#if FOUNDATION_COMPILER_GCC
#  define BITFIELD64  __extension__
#else
#  define BITFIELD64
#endif

#if FOUNDATION_PLATFORM_WINDOWS
//Interlocked* functions are full barrier, no need for explicit semantics
//#  define MEMORY_ORDER_RELEASE() atomic_thread_fence_release()
#  define MEMORY_ORDER_RELEASE() do {} while(0)
#elif FOUNDATION_COMPILER_GCC
//GCC __sync_* are full barrier, no need for explicit semantics
//#  define MEMORY_ORDER_RELEASE() atomic_thread_fence_release()
#  define MEMORY_ORDER_RELEASE() do {} while(0)
#else
#  error Define memory barrier/fence
#endif

#define MEMORY_ALIGN_MIN                        FOUNDATION_ARCH_POINTER_SIZE
#define MEMORY_ALIGN_MAX                        16

#define MEMORY_POINTER_HIGH_MASK_BITS           6
#define MEMORY_POINTER_LOW_MASK_BITS            6
#define MEMORY_POINTER_BITS                     52  //Masked out 6 high and 6 low bits (ok since sizeof(descriptor)==64 and aligned to page boundaries in alloc), so (pointer<<6) to get pointer and (pointer>>6) to set pointer.
#define MEMORY_TAG_BITS                         12  //This gives us a loop of 4096 iterations, which should be reasonably ABA safe
#define MEMORY_CREDIT_BITS                      MEMORY_TAG_BITS
#define MEMORY_MAX_CREDITS                      ((1<<MEMORY_CREDIT_BITS)-1)

#define MEMORY_MASK_POINTER( p )                ( (uintptr_t)(p) >> MEMORY_POINTER_LOW_MASK_BITS )
#define MEMORY_UNMASK_POINTER( m )              (void*)( (uintptr_t)(m) << MEMORY_POINTER_LOW_MASK_BITS )

typedef struct _memory_anchor                   memory_anchor_t;
typedef struct _memory_pointer                  memory_pointer_t;
typedef struct _memory_descriptor               memory_descriptor_t;
typedef struct _memory_sizeclass                memory_sizeclass_t;
typedef struct _memory_heap                     memory_heap_t;
typedef struct _memory_heap_pool                memory_heap_pool_t;
typedef struct _memory_descriptor_list_pointer  memory_descriptor_list_pointer_t;

enum {
	MEMORY_ANCHOR_ACTIVE  = 0,
	MEMORY_ANCHOR_FULL    = 1,
	MEMORY_ANCHOR_PARTIAL = 2,
	MEMORY_ANCHOR_EMPTY   = 3
};

struct _memory_anchor {
	BITFIELD64 uint64_t                         available:12;
	BITFIELD64 uint64_t                         count:12;
	BITFIELD64 uint64_t                         state:2;
	BITFIELD64 uint64_t                         tag:38;
};

typedef union {
	memory_anchor_t                             anchor;
	atomic64_t                                  raw;
} memory_anchor_value_t;
FOUNDATION_STATIC_ASSERT(sizeof(memory_anchor_t) == sizeof(uint64_t), "anchdor size");
FOUNDATION_STATIC_ASSERT(sizeof(memory_anchor_value_t) == sizeof(uint64_t), "anchor value size");

struct _memory_descriptor {
	memory_anchor_value_t                        anchor;
	memory_descriptor_t*                         next;
	memory_heap_t*                               heap;
	void*                                        superblock;
	unsigned int                                 size;
	unsigned int                                 max_count;
#if FOUNDATION_ARCH_POINTER_SIZE == 4
	char                                         pad[36];
#else
	char                                         pad[24];
#endif
};
//The structure needs to align to 64 bytes for pointer/tag union to work with descriptor blocks
FOUNDATION_STATIC_ASSERT((sizeof(memory_descriptor_t)&63) == 0, "descriptor size");

struct _memory_descriptor_list_pointer {
BITFIELD64 uint64_t                          pointer:MEMORY_POINTER_BITS;
BITFIELD64 uint64_t                          tag:MEMORY_TAG_BITS;
};
typedef union {
	memory_descriptor_list_pointer_t             ptr;
	atomic64_t                                   raw;
} memory_descriptor_pointer_t;
FOUNDATION_STATIC_ASSERT(sizeof(memory_descriptor_list_pointer_t) == sizeof(uint64_t),
                         "pointer size");
FOUNDATION_STATIC_ASSERT(sizeof(memory_descriptor_pointer_t) == sizeof(uint64_t), "pointer size");

struct _memory_pointer {
BITFIELD64 uint64_t                          pointer:MEMORY_POINTER_BITS;
BITFIELD64 uint64_t                          credits:MEMORY_CREDIT_BITS;
};
typedef union {
	memory_pointer_t                             ptr;
	atomic64_t                                   raw;
} memory_pointer_value_t;
FOUNDATION_STATIC_ASSERT(sizeof(memory_pointer_t) == sizeof(uint64_t), "pointer size");
FOUNDATION_STATIC_ASSERT(sizeof(memory_pointer_value_t) == sizeof(uint64_t), "pointer size");

struct _memory_sizeclass {
	memory_descriptor_pointer_t                  partial;
	unsigned int                                 block_size;
	unsigned int                                 superblock_size;
};

struct _memory_heap {
	memory_pointer_value_t                       active;
	memory_descriptor_pointer_t                  partial;
#if BUILD_USE_HEAP_PENDING_SUPERBLOCK
	atomicptr_t                                  pending_superblock;
#endif
	memory_sizeclass_t*                          size_class;
};

struct _memory_heap_pool {
	memory_heap_t*                               heaps;
};

static void*
_memory_allocate_superblock(size_t size, bool raw);

static void
_memory_free_superblock(void* superblock, size_t size);

static void*
_memory_malloc_from_active(memory_heap_t* heap);

static void*
_memory_malloc_from_partial(memory_heap_t* heap);

static void*
_memory_malloc_from_new(memory_heap_t* heap);

static memory_heap_t*
_memory_find_heap(size_t size);

static memory_descriptor_t*
_memory_allocate_descriptor(void);

static void
_memory_retire_descriptor(memory_heap_t* heap, memory_descriptor_t* descriptor);
static void
_memory_remove_empty_descriptor(memory_heap_t* heap, memory_descriptor_t* descriptor);

static void
_memory_heap_put_partial(memory_heap_t* heap, memory_descriptor_t* descriptor);

static memory_descriptor_t*
_memory_heap_get_partial(memory_heap_t* heap);

static memory_descriptor_t*
_memory_partial_list_get(memory_sizeclass_t* size_class);

static void
_memory_partial_list_put(memory_sizeclass_t* size_class, memory_descriptor_t* descriptor);

static void
_memory_partial_list_remove_empty(memory_sizeclass_t* size_class);

static void*
_memory_allocate(hash_t context, size_t size, unsigned int align, unsigned int hint);

static void*
_memory_reallocate(void* p, uint64_t size, unsigned int align, uint64_t oldsize);

static void
_memory_deallocate(void* p);

static memory_descriptor_pointer_t _memory_descriptor_available;
static memory_sizeclass_t* _memory_sizeclass;
static unsigned int _memory_num_sizeclass;
static memory_heap_pool_t* _memory_heap_pool;
static unsigned int _memory_num_heap_pool;
static atomic32_t _memory_heap_counter;
static memory_detailed_statistics_t _memory_statistics;

#if FOUNDATION_PLATFORM_WINDOWS

#include <foundation/windows.h>

static void*
_memory_allocate_superblock(size_t size, bool raw) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_incr64(&_memory_statistics.allocations_current);
	atomic_incr64(&_memory_statistics.allocations_total);
	atomic_add64(&_memory_statistics.allocated_current, (int64_t)size);
	atomic_add64(&_memory_statistics.allocated_total, (int64_t)size);
#endif
	return VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE /*| ( raw ? MEM_TOP_DOWN : 0 )*/,
	                    PAGE_READWRITE);
}

static void
_memory_free_superblock(void* ptr, size_t size) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_decr64(&_memory_statistics.allocations_current_raw);
	atomic_add64(&_memory_statistics.allocated_current_raw, -(int64_t)size);
#endif
	VirtualFree(ptr, 0, MEM_RELEASE);
}


#elif FOUNDATION_PLATFORM_POSIX

#include <foundation/posix.h>
#include <sys/mman.h>

static void*
_memory_allocate_superblock(size_t size, bool raw) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_incr64(&_memory_statistics.allocations_current_raw);
	atomic_incr64(&_memory_statistics.allocations_total_raw);
	atomic_add64(&_memory_statistics.allocated_current_raw, (int64_t)size);
	atomic_add64(&_memory_statistics.allocated_total_raw, (int64_t)size);
#endif
#ifndef MAP_UNINITIALIZED
#define MAP_UNINITIALIZED 0
#endif
	void* block = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED,
	                   -1, 0);
	return block;
}

static void
_memory_free_superblock(void* ptr, size_t size) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_decr64(&_memory_statistics.allocations_current_raw);
	atomic_add64(&_memory_statistics.allocated_current_raw, -(int64_t)size);
#endif
	munmap(ptr, size);
}

#else
#  error Not implemented
#endif

static FOUNDATION_PURECALL memory_heap_t*
_memory_find_heap(size_t size) {
	unsigned int ipool;
	unsigned int iheap;
	memory_heap_pool_t* pool;

	size += sizeof(void*);   // Make sure we can fit prefix in

	ipool = atomic_incr32(&_memory_heap_counter) /*thread_id()*/ % _memory_num_heap_pool;
	pool = _memory_heap_pool + ipool;

	for (iheap = 0; iheap < _memory_num_sizeclass; ++iheap) {
		if (pool->heaps[iheap].size_class->block_size >= size) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
			atomic_incr64(&_memory_statistics.allocations_calls_heap_pool[ipool]);
#endif
			return pool->heaps + iheap;
		}
	}

	return 0;
}

//Alignment not needed, all superblocks are page aligned and blocks are at least 32 bytes
//which results in all blocks being 32 byte aligned
//static CONSTCALL FORCEINLINE unsigned int _memory_get_align( unsigned int align )
//{
//#if FOUNDATION_PLATFORM_ANDROID
//	return align ? MEMORY_ALIGN_MAX : 0;
//#else
//	if( align < FOUNDATION_ARCH_POINTER_SIZE )
//		return align ? FOUNDATION_ARCH_POINTER_SIZE : 0;
//	align = math_align_poweroftwo( align );
//	return ( align < MEMORY_ALIGN_MAX ) ? align : MEMORY_ALIGN_MAX;
//#endif
//}

static void
_memory_heap_put_partial(memory_heap_t* heap, memory_descriptor_t* descriptor) {
	memory_descriptor_t* prev;
	memory_descriptor_pointer_t old_partial;
	memory_descriptor_pointer_t new_partial;

	do {
		old_partial.raw = heap->partial.raw;
		new_partial.raw = old_partial.raw;
		prev = MEMORY_UNMASK_POINTER(old_partial.ptr.pointer);
		new_partial.ptr.pointer = MEMORY_MASK_POINTER(descriptor);
		++new_partial.ptr.tag;
	}
	while (!atomic_cas64(&heap->partial.raw, new_partial.raw.nonatomic, old_partial.raw.nonatomic));

	if (prev)
		_memory_partial_list_put(heap->size_class, prev);
}

static memory_descriptor_t*
_memory_heap_get_partial(memory_heap_t* heap) {
	memory_descriptor_t* descriptor;
	memory_descriptor_pointer_t old_partial;
	memory_descriptor_pointer_t new_partial;

	do {
		old_partial.raw = heap->partial.raw;
		new_partial.raw = old_partial.raw;
		descriptor = MEMORY_UNMASK_POINTER(old_partial.ptr.pointer);
		if (!descriptor)
			return _memory_partial_list_get(heap->size_class);
		new_partial.ptr.pointer = 0;
		++new_partial.ptr.tag;
	}
	while (!atomic_cas64(&heap->partial.raw, new_partial.raw.nonatomic, old_partial.raw.nonatomic));

	return descriptor;
}

static void
_memory_partial_list_put(memory_sizeclass_t* size_class, memory_descriptor_t* descriptor) {
	memory_descriptor_pointer_t old_partial;
	memory_descriptor_pointer_t new_partial;

	do {
		//TODO: improve, this potentially contends too much with _partial_list_get
		old_partial.raw = size_class->partial.raw;
		new_partial.raw = old_partial.raw;
		descriptor->next = MEMORY_UNMASK_POINTER(old_partial.ptr.pointer);
		new_partial.ptr.pointer = MEMORY_MASK_POINTER(descriptor);
		++new_partial.ptr.tag;
	}
	while (!atomic_cas64(&size_class->partial.raw, new_partial.raw.nonatomic,
	                     old_partial.raw.nonatomic));
}

static memory_descriptor_t*
_memory_partial_list_get(memory_sizeclass_t* size_class) {
	memory_descriptor_t* descriptor;
	memory_descriptor_pointer_t old_partial;
	memory_descriptor_pointer_t new_partial;

	do {
		old_partial.raw = size_class->partial.raw;
		new_partial.raw = old_partial.raw;
		descriptor = MEMORY_UNMASK_POINTER(old_partial.ptr.pointer);
		if (!descriptor)
			return 0;
		new_partial.ptr.pointer = MEMORY_MASK_POINTER(descriptor->next);
		++new_partial.ptr.tag;
	}
	while (!atomic_cas64(&size_class->partial.raw, new_partial.raw.nonatomic,
	                     old_partial.raw.nonatomic));

	return descriptor;
}

static void
_memory_partial_list_remove_empty(memory_sizeclass_t* size_class) {
	unsigned int retired = 0;
	bool is_empty = false;

	memory_descriptor_t* descriptor;
	memory_descriptor_t* first;
	memory_descriptor_t* next;
	memory_descriptor_t* last;
	memory_descriptor_pointer_t old_partial;
	memory_descriptor_pointer_t new_partial;

	// Pre-walking the list is safe since descriptor memory is never actually deallocated and
	// returned to system, so any pointers will be safe to read (even if it may yield false
	// positives, which we don't care about - only means we will grab list ownership a bit too often)
	descriptor = MEMORY_UNMASK_POINTER(size_class->partial.ptr.pointer);
	while (descriptor && !is_empty) {
		is_empty = (descriptor->anchor.anchor.state == MEMORY_ANCHOR_EMPTY);
		descriptor = descriptor->next;
	}

	if (!is_empty)
		return;

	//TODO: Potentially improve this, in order to maintain thread safety we grab ownership of
	//      entire list by replacing head pointer with 0, then modify and free up the descriptors
	//      in the local list, and finally restoring the partial descriptors we got left in list
	//      to the size class partial list
	do {
		old_partial.raw = size_class->partial.raw;
		new_partial.raw = old_partial.raw;
		descriptor = MEMORY_UNMASK_POINTER(old_partial.ptr.pointer);
		if (!descriptor)
			return;
		new_partial.ptr.pointer = 0;
		++new_partial.ptr.tag;
	}
	while (!atomic_cas64(&size_class->partial.raw, new_partial.raw.nonatomic,
	                     old_partial.raw.nonatomic));

	first = descriptor;
	last = 0;

	while (descriptor) {
		next = descriptor->next;

		if (descriptor->anchor.anchor.state == MEMORY_ANCHOR_EMPTY) {
			if (last)
				last->next = next;
			if (first == descriptor)
				first = next;

			_memory_retire_descriptor(descriptor->heap, descriptor);
			++retired;
		}
		else {
			last = descriptor;
		}
		descriptor = next;
	}

	if (last) do {
			old_partial.raw = size_class->partial.raw;
			new_partial.raw = old_partial.raw;
			last->next = MEMORY_UNMASK_POINTER(old_partial.ptr.pointer);
			new_partial.ptr.pointer = MEMORY_MASK_POINTER(first);
			++new_partial.ptr.tag;
		}
		while (!atomic_cas64(&size_class->partial.raw, new_partial.raw.nonatomic,
		                     old_partial.raw.nonatomic));

	log_memory_spamf("<< _memory_partial_list_remove_empty( %u ) : %u", size_class->block_size,
	                 retired);
}

static memory_descriptor_t*
_memory_allocate_descriptor(void) {
	memory_descriptor_t* descriptor;
	memory_descriptor_t* next;

	memory_descriptor_pointer_t new_available;
	memory_descriptor_pointer_t old_available;

	//Grab existing
	do {
		old_available.raw = _memory_descriptor_available.raw;
		new_available.raw = old_available.raw;
		descriptor = MEMORY_UNMASK_POINTER(old_available.ptr.pointer);
		if (!descriptor)
			break;
		new_available.ptr.pointer = MEMORY_MASK_POINTER(descriptor->next);
		++new_available.ptr.tag;
		MEMORY_ORDER_RELEASE();
		if (atomic_cas64(&_memory_descriptor_available.raw, new_available.raw.nonatomic,
		                 old_available.raw.nonatomic)) {
			if (descriptor)
				return descriptor;
		}
	}
	while (descriptor);

	//Allocate new block of descriptors
	{
		size_t i;
		//TODO: Proper page size from system info
		int page_size = 4096;
		size_t size = page_size * 16;
		size_t num_descriptors;
		memory_descriptor_t* first;
		memory_descriptor_t* last;
		void* descblock = _memory_allocate_superblock(size, true);
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		atomic_incr64(&_memory_statistics.allocations_new_descriptor_superblock);
#endif
		//Align pointer to 64 bytes
		if ((uintptr_t)descblock & 63) {
			descblock = (char*)descblock + (64 - ((uintptr_t)descblock & 63));
			num_descriptors = (size - 64) / sizeof(memory_descriptor_t);
		}
		else {
			num_descriptors = size / sizeof(memory_descriptor_t);
		}

		descriptor = descblock;
		next = descriptor;
		for (i = 0; i < num_descriptors - 1; ++i) {
			next->next = (memory_descriptor_t*)pointer_offset(next, sizeof(memory_descriptor_t));
			next = (memory_descriptor_t*)next->next;
		}
		last = next;
		first = (memory_descriptor_t*)descriptor->next;

		do {
			old_available.raw = _memory_descriptor_available.raw;
			new_available.raw = old_available.raw;
			new_available.ptr.pointer = MEMORY_MASK_POINTER(first);
			++new_available.ptr.tag;
			last->next = MEMORY_UNMASK_POINTER(old_available.ptr.pointer);
			MEMORY_ORDER_RELEASE();
		}
		while (!atomic_cas64(&_memory_descriptor_available.raw, new_available.raw.nonatomic,
		                     old_available.raw.nonatomic));
		//NOTE: if memory conservation is more important, free superblock here and retry instead of cas-looping and setting pointer
	}

	log_memory_debugf("<< _memory_allocate_descriptor() : %p", descriptor);
	return descriptor;
}

static void
_memory_retire_descriptor(memory_heap_t* heap, memory_descriptor_t* descriptor) {
	memory_descriptor_pointer_t new_available;
	memory_descriptor_pointer_t old_available;

	descriptor->heap = 0;

	if (descriptor->superblock) {
#if BUILD_USE_HEAP_PENDING_SUPERBLOCK
		if (heap->pending_superblock.nonatomic ||
		        !atomic_cas_ptr(&heap->pending_superblock, descriptor->superblock, 0))
#endif
			_memory_free_superblock(descriptor->superblock, heap->size_class->superblock_size);
	}
	descriptor->superblock = 0;

	do {
		old_available.raw = _memory_descriptor_available.raw;
		new_available.raw = old_available.raw;
		descriptor->next = MEMORY_UNMASK_POINTER(old_available.ptr.pointer);
		new_available.ptr.pointer = MEMORY_MASK_POINTER(descriptor);
		++new_available.ptr.tag;
		MEMORY_ORDER_RELEASE();
	}
	while (!atomic_cas64(&_memory_descriptor_available.raw, new_available.raw.nonatomic,
	                     old_available.raw.nonatomic));
}

static void
_memory_remove_empty_descriptor(memory_heap_t* heap, memory_descriptor_t* descriptor) {
	memory_descriptor_pointer_t old_partial;
	memory_descriptor_pointer_t new_partial;

	//Retire if it was heap partial and we manage to remove it
	old_partial.raw = heap->partial.raw;
	if (old_partial.ptr.pointer == MEMORY_MASK_POINTER(descriptor)) {
		new_partial.ptr.pointer = 0;
		new_partial.ptr.tag = old_partial.ptr.tag + 1;
		if (atomic_cas64(&heap->partial.raw, new_partial.raw.nonatomic, old_partial.raw.nonatomic)) {
			if (descriptor->anchor.anchor.state == MEMORY_ANCHOR_EMPTY)
				_memory_retire_descriptor(heap, descriptor);
			else
				_memory_heap_put_partial(heap, descriptor);
		}
		return;
	}

	_memory_partial_list_remove_empty(heap->size_class);
}

static bool
_memory_update_active_superblock(memory_heap_t* heap, memory_descriptor_t* descriptor,
                                 unsigned int more_credits) {
	memory_pointer_value_t new_active;
	memory_anchor_value_t new_anchor, old_anchor;

	new_active.ptr.pointer = MEMORY_MASK_POINTER(descriptor);
	new_active.ptr.credits = more_credits - 1;
	if (atomic_cas64(&heap->active.raw, new_active.raw.nonatomic, 0))
		return true;

	//Someone else installed another active superblock
	//Return credits and make superblock partial
	do {
		old_anchor.raw = descriptor->anchor.raw;
		new_anchor.raw = old_anchor.raw;
		new_anchor.anchor.count += more_credits;
		new_anchor.anchor.state = MEMORY_ANCHOR_PARTIAL;
	}
	while (!atomic_cas64(&descriptor->anchor.raw, new_anchor.raw.nonatomic, old_anchor.raw.nonatomic));

	_memory_heap_put_partial(heap, descriptor);

	return false;
}

static void*
_memory_malloc_from_active(memory_heap_t* heap) {
	memory_pointer_value_t old_active, new_active;
	memory_anchor_value_t old_anchor, new_anchor;
	memory_descriptor_t* descriptor;
	void* address;
	unsigned int more_credits;

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
	atomic_incr64(&_memory_statistics.allocations_calls_active);
#endif

	do { //Reserve block
		old_active.raw = heap->active.raw;
		if (!old_active.ptr.pointer) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
			atomic_incr64(&_memory_statistics.allocations_calls_active_no_active);
#endif
			return 0;
		}
		if (!old_active.ptr.credits)
			new_active.raw.nonatomic = 0;
		else {
			new_active.raw = old_active.raw;
			--new_active.ptr.credits;
		}
	}
	while (!atomic_cas64(&heap->active.raw, new_active.raw.nonatomic, old_active.raw.nonatomic));

	//Pop block
	descriptor = MEMORY_UNMASK_POINTER(old_active.ptr.pointer);

	//Descriptor may transition heaps if punted to heap partial and then to size_class partial,
	//and finally picked up by another heap.
	//FOUNDATION_ASSERT_MSG( descriptor->heap == heap, "Descriptor heap mismatch during alloc from active" );

	more_credits = 0;
	do {
		//State may be ACTIVE, PARTIAL or FULL
		unsigned int next;
		old_anchor.raw = descriptor->anchor.raw;
		new_anchor.raw = old_anchor.raw;

#if BUILD_USE_PRE_ALIGN
		address = (char*)descriptor->superblock + (((old_anchor.anchor.available + 1) * descriptor->size) -
		                                           sizeof(void*));
#else
		address = (char*)descriptor->superblock + (old_anchor.anchor.available * descriptor->size);
#endif
		next = *(unsigned int*)address;
		new_anchor.anchor.available = next;
		++new_anchor.anchor.tag;

		if (old_active.ptr.credits == 0) {
			if (old_anchor.anchor.count == 0) {
				new_anchor.anchor.state = MEMORY_ANCHOR_FULL;
			}
			else {
				more_credits = (old_anchor.anchor.count < MEMORY_MAX_CREDITS) ? (unsigned int)
				               old_anchor.anchor.count : MEMORY_MAX_CREDITS;
				new_anchor.anchor.count -= more_credits;
			}
		}
	}
	while (!atomic_cas64(&descriptor->anchor.raw, new_anchor.raw.nonatomic, old_anchor.raw.nonatomic));

	if ((old_active.ptr.credits == 0) && (old_anchor.anchor.count > 0)) {
		bool active = _memory_update_active_superblock(heap, descriptor, more_credits);
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		if (active)
			atomic_incr64(&_memory_statistics.allocations_calls_active_to_active);
		else
			atomic_incr64(&_memory_statistics.allocations_calls_active_to_partial);
#endif
	}
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
	else if (old_active.ptr.credits == 0) {
		atomic_incr64(&_memory_statistics.allocations_calls_active_to_full);
	}
	else {
		atomic_incr64(&_memory_statistics.allocations_calls_active_credits);
	}
#endif

	*(void**)address = descriptor;
	address = pointer_offset(address, sizeof(void*));
	//FOUNDATION_ASSERT_MSG( ( (uintptr_t)address & 31 ) == 0, "Address align failure" );

	return address;
}

static void*
_memory_malloc_from_partial(memory_heap_t* heap) {
	memory_anchor_value_t old_anchor, new_anchor;
	void* address;
	memory_descriptor_t* descriptor;
	unsigned int more_credits;

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
	atomic_incr64(&_memory_statistics.allocations_calls_partial);
#endif

retry:

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
	atomic_incr64(&_memory_statistics.allocations_calls_partial_tries);
#endif

	descriptor = _memory_heap_get_partial(heap);
	if (!descriptor) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		atomic_incr64(&_memory_statistics.allocations_calls_partial_no_descriptor);
#endif
		return 0;
	}

	descriptor->heap = heap;
	do {
		//Reserve blocks
		old_anchor.raw = descriptor->anchor.raw;
		new_anchor.raw = old_anchor.raw;
		if (old_anchor.anchor.state == MEMORY_ANCHOR_EMPTY) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
			atomic_incr64(&_memory_statistics.allocations_calls_partial_to_retire);
#endif
			_memory_retire_descriptor(heap, descriptor);
			goto retry;
		}
		else if (old_anchor.anchor.state != MEMORY_ANCHOR_PARTIAL) {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
			atomic_incr64(&_memory_statistics.allocations_calls_partial_no_descriptor);
#endif
			return 0;
		}
		else {
			//old anchor state must be PARTIAL, count must be > 0
			FOUNDATION_ASSERT_MSGFORMAT(old_anchor.anchor.state == MEMORY_ANCHOR_PARTIAL,
			                            "Unexpected anchor state, got %u, wanted %u", (unsigned int)old_anchor.anchor.state,
			                            MEMORY_ANCHOR_PARTIAL);
			more_credits = (old_anchor.anchor.count - 1 < MEMORY_MAX_CREDITS ? (unsigned int)
			                old_anchor.anchor.count - 1 : MEMORY_MAX_CREDITS);
		}
		new_anchor.anchor.count -= more_credits + 1;
		new_anchor.anchor.state = (more_credits > 0) ? MEMORY_ANCHOR_ACTIVE : MEMORY_ANCHOR_FULL;

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		if (new_anchor.anchor.state == MEMORY_ANCHOR_ACTIVE)
			atomic_incr64(&_memory_statistics.allocations_calls_partial_to_active);
		else
			atomic_incr64(&_memory_statistics.allocations_calls_partial_to_full);
#endif

	}
	while (!atomic_cas64(&descriptor->anchor.raw, new_anchor.raw.nonatomic, old_anchor.raw.nonatomic));

	do {
		//Pop reserved block
		old_anchor.raw = descriptor->anchor.raw;
		new_anchor.raw = old_anchor.raw;
#if BUILD_USE_PRE_ALIGN
		address = (char*)descriptor->superblock + (((old_anchor.anchor.available + 1) * descriptor->size) -
		                                           sizeof(void*));
#else
		address = (char*)descriptor->superblock + (old_anchor.anchor.available * descriptor->size);
#endif
		new_anchor.anchor.available = *(unsigned int*)address;
		++new_anchor.anchor.tag;
	}
	while (!atomic_cas64(&descriptor->anchor.raw, new_anchor.raw.nonatomic, old_anchor.raw.nonatomic));

	if (more_credits > 0)
		_memory_update_active_superblock(heap, descriptor, more_credits);

	*(void**)address = descriptor;
	address = pointer_offset(address, sizeof(void*));
	//FOUNDATION_ASSERT_MSG( ( (uintptr_t)address & 31 ) == 0, "Address align failure" );

	return address;
}

static void*
_memory_malloc_from_new(memory_heap_t* heap) {
	void* address;
	memory_pointer_value_t new_active;
	unsigned int credits;
	unsigned int i;
	memory_descriptor_t* descriptor;
	bool from_pending = true;
	uintptr_t offset = 0;
	void* base_address;

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
	atomic_incr64(&_memory_statistics.allocations_calls_new_block);
#endif

	descriptor = _memory_allocate_descriptor();
	descriptor->heap = heap;
	descriptor->superblock = 0;
	descriptor->anchor.anchor.available = 1; //We're allocating the first available block at index 0
	descriptor->size = heap->size_class->block_size;
#if BUILD_USE_PRE_ALIGN
	descriptor->max_count = (heap->size_class->superblock_size / descriptor->size) -
	                        1;   //Use first block for alignment and clobber watch overhead
#else
	descriptor->max_count = (heap->size_class->superblock_size / descriptor->size);
#endif

	credits = descriptor->max_count - 1;
	if (credits > MEMORY_MAX_CREDITS)
		credits = MEMORY_MAX_CREDITS;
	new_active.ptr.pointer = MEMORY_MASK_POINTER(descriptor);
	new_active.ptr.credits = credits - 1;
	descriptor->anchor.anchor.count = (descriptor->max_count - 1) - credits;
	descriptor->anchor.anchor.state = MEMORY_ANCHOR_ACTIVE;

	//Early out if new block was put in place by other thread
	if (atomic_load64(&heap->active.raw) || heap->partial.ptr.pointer ||
	        heap->size_class->partial.ptr.pointer) {
		_memory_retire_descriptor(heap, descriptor);
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		atomic_incr64(&_memory_statistics.allocations_new_block_earlyouts);
#endif
		return 0;
	}

	//Try to grab any existing pending superblock
#if BUILD_USE_HEAP_PENDING_SUPERBLOCK
	descriptor->superblock = atomic_loadptr(&heap->pending_superblock);
	if (!descriptor->superblock ||
	        !atomic_cas_ptr(&heap->pending_superblock, 0, descriptor->superblock))
#endif
	{
		descriptor->superblock = _memory_allocate_superblock(heap->size_class->superblock_size, false);
		from_pending = false;
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		atomic_incr64(&_memory_statistics.allocations_new_block_superblock);
#endif
	}

	//Make linked list of next available block
#if BUILD_USE_PRE_ALIGN
	base_address = pointer_offset(descriptor->superblock, descriptor->size - sizeof(void*));
#else
	base_address = descriptor->superblock;
#endif
	for (i = 0, offset = 0; i < descriptor->max_count; ++i, offset += descriptor->size)
		*(unsigned int*)pointer_offset(base_address, offset) = i + 1;

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
	if (from_pending)
		atomic_incr64(&_memory_statistics.allocations_new_block_pending_hits);
#endif

	MEMORY_ORDER_RELEASE();
	if (atomic_cas64(&heap->active.raw, new_active.raw.nonatomic, 0)) {
		//First block in superblock reserved for alignment and clobber watch overhead
		address = base_address;
		*(void**)address = descriptor;
		address = pointer_offset(address, sizeof(void*));     //Address is now (at least) 32 byte aligned
		//FOUNDATION_ASSERT_MSG( ( (uintptr_t)address & 31 ) == 0, "Address align failure" );

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		if (from_pending)
			atomic_incr64(&_memory_statistics.allocations_new_block_pending_success);
		else
			atomic_incr64(&_memory_statistics.allocations_new_block_superblock_success);
#endif

		return address;
	}

#if BUILD_USE_HEAP_PENDING_SUPERBLOCK
	//This is an optimization that tries to save this newly allocated (but not installed) superblock
	//as pending, so next new allocation can grab it immediately. This (potentially) saves us
	//one free and one alloc of a new superblock.
	if (atomic_cas_ptr(&heap->pending_superblock, descriptor->superblock, 0)) {
		descriptor->superblock = 0;
		_memory_retire_descriptor(heap, descriptor);

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		if (from_pending)
			atomic_incr64(&_memory_statistics.allocations_new_block_pending_stores);
		else
			atomic_incr64(&_memory_statistics.allocations_new_block_superblock_stores);
#endif
		return 0;
	}
#endif

	_memory_free_superblock(descriptor->superblock, heap->size_class->superblock_size);

	descriptor->superblock = 0;
	_memory_retire_descriptor(heap, descriptor);

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
	if (from_pending)
		atomic_incr64(&_memory_statistics.allocations_new_block_pending_deallocations);
	else
		atomic_incr64(&_memory_statistics.allocations_new_block_superblock_deallocations);
#endif

	return 0;
}

static void*
_memory_allocate(hash_t context, size_t size, unsigned int align, unsigned int hint) {
	void* ptr = 0;
	memory_heap_t* heap = _memory_find_heap((size_t)size);
	if (!heap) {
		uintptr_t* block;

		//Large block, allocate from OS virtual memory directly and set low bit as identifier in prefix
		size += sizeof(void*);
		if (size & 1)
			++size;

		block = _memory_allocate_superblock((size_t)size, true);
		*block = (uintptr_t)size | 1;

		ptr = pointer_offset(block, sizeof(void*));

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		atomic_incr64(&_memory_statistics.allocations_calls_oversize);
#endif
	}
	else {
		size = heap->size_class->block_size;

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
		atomic_incr64(&_memory_statistics.allocations_calls_heap);
#endif

		do {
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS > 1
			atomic_incr64(&_memory_statistics.allocations_calls_heap_loops);
#endif

			ptr = _memory_malloc_from_active(heap);
			if (ptr) break;
			ptr = _memory_malloc_from_partial(heap);
			if (ptr) break;
			ptr = _memory_malloc_from_new(heap);
		}
		while (!ptr);
	}

	if (ptr && (hint & MEMORY_ZERO_INITIALIZED))
		memset(ptr, 0, (size_t)size);

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_add64(&_memory_statistics.allocated_current, size);
	atomic_add64(&_memory_statistics.allocated_total, size);
	atomic_incr64(&_memory_statistics.allocations_current);
	atomic_incr64(&_memory_statistics.allocations_total);
#endif
	return ptr;
}

static void*
_memory_reallocate(void* p, uint64_t size, unsigned int align, uint64_t oldsize) {
	uint64_t need_size;
	void* raw_descriptor;
	memory_descriptor_t* descriptor;
	memory_heap_t* heap;
	void* block;

	if (p) {
		//Check if we can fit new block in old
		void* p_raw = (uintptr_t*)p - 1; //Back up to prefix
		if ((*((uintptr_t*)p_raw)) & 1) {
			if ((size <= oldsize) && (size >= (oldsize >> 1)))
				return p;
		}
		else {
			raw_descriptor = *(void**)p_raw;
			descriptor = (memory_descriptor_t*)raw_descriptor;
			heap = descriptor->heap;
			need_size = size + sizeof(void*);
			if ((need_size <= heap->size_class->block_size) &&
			        (need_size >= (heap->size_class->block_size >> 1)))
				return p;
		}
	}

	block = _memory_allocate(memory_context(), size, align, MEMORY_PERSISTENT);
	if (block && p && oldsize)
		memcpy(block, p, (size_t)((size < oldsize) ? size : oldsize));
	_memory_deallocate(p);
	return block;
}

static void
_memory_deallocate(void* p) {
	uint64_t size = 0;
	void* raw_descriptor;
	void* superblock;
	void* base_address;
	memory_descriptor_t* descriptor;
	memory_anchor_value_t new_anchor, old_anchor;
	memory_heap_t* heap;

	if (!p)
		return;

	p = (uintptr_t*)p - 1; //Back up to prefix

	if ((*((uintptr_t*)p)) &
	        1) { //A descriptor pointer never has low bit set, used as identifier for large block allocated directly from OS
		size = (*(uintptr_t*)p) - 1;
#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
		atomic_add64(&_memory_statistics.allocated_current, -(int64_t)size);
		atomic_decr64(&_memory_statistics.allocations_current);
#endif
		_memory_free_superblock(p, (size_t)size);
		log_memory_debugf("<< memory_deallocate() : superblock", p);
		return;
	}

	raw_descriptor = *(void**)p;
	//FOUNDATION_ASSERT_MSGFORMAT( (uintptr_t)raw_descriptor > 0x10000, "Bad raw pointer: " STRING_FORMAT_POINTER, raw_descriptor );
	descriptor = (memory_descriptor_t*)raw_descriptor;

	superblock = descriptor->superblock;
	heap = descriptor->heap;
#if BUILD_USE_PRE_ALIGN
	base_address = pointer_offset(superblock, descriptor->size - sizeof(void*));
#else
	base_address = superblock;
#endif
	do {
		old_anchor.raw = descriptor->anchor.raw;
		new_anchor.raw = old_anchor.raw;
		*(unsigned int*)p = (unsigned int)old_anchor.anchor.available;
		new_anchor.anchor.available = ((uintptr_t)p - (uintptr_t)base_address) / descriptor->size;
		if (old_anchor.anchor.state == MEMORY_ANCHOR_FULL)
			new_anchor.anchor.state = MEMORY_ANCHOR_PARTIAL;
		if (old_anchor.anchor.count == (descriptor->max_count - 1)) {
			MEMORY_ORDER_RELEASE();
			new_anchor.anchor.state = MEMORY_ANCHOR_EMPTY;
		}
		else
			++new_anchor.anchor.count;
		MEMORY_ORDER_RELEASE();
	}
	while (!atomic_cas64(&descriptor->anchor.raw, new_anchor.raw.nonatomic, old_anchor.raw.nonatomic));

	if (new_anchor.anchor.state == MEMORY_ANCHOR_EMPTY)
		_memory_remove_empty_descriptor(heap, descriptor);
	else if (old_anchor.anchor.state == MEMORY_ANCHOR_FULL)
		_memory_heap_put_partial(heap, descriptor);

#if BUILD_ENABLE_DETAILED_MEMORY_STATISTICS
	atomic_add64(&_memory_statistics.allocated_current, -(int64_t)heap->size_class->block_size);
	atomic_decr64(&_memory_statistics.allocations_current);
#endif
}

static int
_memory_initialize(void) {
	//Initialize heaps
	unsigned int block_size[11] = {    32,   64,   96,  128,  256, 512, 1024, 4096, 8192, 32768, 65536 };
	unsigned int block_count[11] = { 2048, 1024, 1024, 1024, 1024, 512,  256,  128,   64,    32,    16 };
	unsigned int num_sizeclass = 11;
	unsigned int num_heap_pool = 7;
	unsigned int ipool, iheap, iclass;
	unsigned int size;
	memory_heap_t* base_heap;
	void* block;

	log_memory_debug("Initialize memory library");

	memset(&_memory_statistics, 0, sizeof(_memory_statistics));

	num_heap_pool = (unsigned int)system_hardware_threads() + 1;
	if (num_heap_pool < 3)
		num_heap_pool = 3;
	if (num_heap_pool > 32)
		num_heap_pool = 32;

	size = (sizeof(memory_heap_pool_t) * num_heap_pool) + (num_sizeclass * (sizeof(
	                                                           memory_sizeclass_t) + (sizeof(memory_heap_t) * num_heap_pool)));
	block = _memory_allocate_superblock(size, true);
	memset(block, 0, size);

	_memory_heap_pool = block;

	_memory_sizeclass = (memory_sizeclass_t*)(_memory_heap_pool + num_heap_pool);
	for (iclass = 0; iclass < num_sizeclass; ++iclass) {
		//TODO: Tweak these values (or even profile at runtime per-project)
		_memory_sizeclass[iclass].block_size = block_size[iclass];
		_memory_sizeclass[iclass].superblock_size = block_size[iclass] * block_count[iclass];
	}

	base_heap = (memory_heap_t*)(_memory_sizeclass + num_sizeclass);
	for (ipool = 0; ipool < num_heap_pool; ++ipool) {
		_memory_heap_pool[ipool].heaps = base_heap + (ipool * num_sizeclass);
		for (iheap = 0; iheap < num_sizeclass; ++iheap)
			_memory_heap_pool[ipool].heaps[iheap].size_class = _memory_sizeclass + iheap;
	}

	_memory_num_sizeclass = num_sizeclass;
	_memory_num_heap_pool = num_heap_pool;

	atomic_store64(&_memory_descriptor_available.raw, 0);

	//Preallocate descriptors
	_memory_retire_descriptor(0, _memory_allocate_descriptor());

	return 0;
}

static void
_memory_finalize(void) {
	unsigned int ipool, iheap, iclass, size;
	memory_descriptor_t* descriptor;

	log_memory_debug("Shutdown memory library");

	for (ipool = 0; ipool < _memory_num_heap_pool; ++ipool) {
		for (iheap = 0; iheap < _memory_num_sizeclass; ++iheap) {
			memory_heap_t* heap = _memory_heap_pool[ipool].heaps + iheap;
			memory_descriptor_t* partial;

			partial = MEMORY_UNMASK_POINTER(heap->partial.ptr.pointer);
			if (partial)
				_memory_retire_descriptor(heap, partial);
			atomic_store64(&heap->partial.raw, 0);

			descriptor = MEMORY_UNMASK_POINTER(heap->active.ptr.pointer);
			if (descriptor)
				_memory_retire_descriptor(heap, descriptor);
			atomic_store64(&heap->active.raw, 0);

#if BUILD_USE_HEAP_PENDING_SUPERBLOCK
			{
				void* pending_superblock = atomic_loadptr(&heap->pending_superblock);
				if (pending_superblock)
					_memory_free_superblock(pending_superblock, heap->size_class->superblock_size);
				atomic_storeptr(&heap->pending_superblock, 0);
			}
#endif
		}

		_memory_heap_pool[ipool].heaps = 0;
	}

	for (iclass = 0; iclass < _memory_num_sizeclass; ++iclass) {
		memory_sizeclass_t* size_class = _memory_sizeclass + iclass;

		memory_descriptor_t* partial = MEMORY_UNMASK_POINTER(size_class->partial.ptr.pointer);
		while (partial) {
			memory_descriptor_t* next = (memory_descriptor_t*)partial->next;
			_memory_retire_descriptor(partial->heap, partial);
			partial = next;
		}

		atomic_store64(&size_class->partial.raw, 0);
	}

	descriptor = MEMORY_UNMASK_POINTER(_memory_descriptor_available.ptr.pointer);
	while (descriptor) {
		FOUNDATION_ASSERT_MSG(!descriptor->superblock, "Dangling superblock found in retired descriptor");
		if (descriptor->superblock)
			_memory_free_superblock(descriptor->superblock, descriptor->heap->size_class->superblock_size);
		descriptor->superblock = 0;

		descriptor = (memory_descriptor_t*)descriptor->next;
	}

	size = (sizeof(memory_heap_pool_t) * _memory_num_heap_pool) + (_memory_num_sizeclass * (sizeof(
	            memory_sizeclass_t) + (sizeof(memory_heap_t) * _memory_num_heap_pool)));

	_memory_num_sizeclass = 0;
	_memory_num_heap_pool = 0;

	_memory_free_superblock(_memory_sizeclass, size);

	_memory_sizeclass = 0;
	_memory_heap_pool = 0;

	atomic_store64(&_memory_descriptor_available.raw, 0);
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

memory_detailed_statistics_t
memory_detailed_statistics(void) {
	return _memory_statistics;
}

void memory_statistics_reset(void) {
	_memory_statistics.allocated_total_raw = 0;
	_memory_statistics.allocations_total_raw = 0;

	_memory_statistics.allocated_total = 0;
	_memory_statistics.allocations_total = 0;

	_memory_statistics.allocations_new_descriptor_superblock = 0;
	_memory_statistics.allocations_new_descriptor_superblock_deallocations = 0;

	_memory_statistics.allocations_calls_new_block = 0;
	_memory_statistics.allocations_new_block_earlyouts = 0;
	_memory_statistics.allocations_new_block_superblock = 0;
	_memory_statistics.allocations_new_block_pending_hits = 0;
	_memory_statistics.allocations_new_block_superblock_success = 0;
	_memory_statistics.allocations_new_block_pending_success = 0;
	_memory_statistics.allocations_new_block_superblock_deallocations = 0;
	_memory_statistics.allocations_new_block_pending_deallocations = 0;
	_memory_statistics.allocations_new_block_superblock_stores = 0;
	_memory_statistics.allocations_new_block_pending_stores = 0;

	_memory_statistics.allocations_calls_oversize = 0;
	_memory_statistics.allocations_calls_heap = 0;
	_memory_statistics.allocations_calls_heap_loops = 0;

	_memory_statistics.allocations_calls_active = 0;
	_memory_statistics.allocations_calls_active_no_active = 0;
	_memory_statistics.allocations_calls_active_to_partial = 0;
	_memory_statistics.allocations_calls_active_to_active = 0;
	_memory_statistics.allocations_calls_active_to_full = 0;
	_memory_statistics.allocations_calls_active_credits = 0;

	_memory_statistics.allocations_calls_partial = 0;
	_memory_statistics.allocations_calls_partial_tries = 0;
	_memory_statistics.allocations_calls_partial_no_descriptor = 0;
	_memory_statistics.allocations_calls_partial_to_retire = 0;
	_memory_statistics.allocations_calls_partial_to_full = 0;
}
