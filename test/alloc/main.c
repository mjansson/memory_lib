/* main.c  -  Memory allocation tests  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a cross-platform memory allocation library in C11 providing basic support data types and
 * functions to write applications and games in a platform-independent fashion. The latest source code is
 * always available at
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
#include <test/test.h>

#include <memory/memory.h>

#include <stdio.h>

static application_t
test_alloc_application(void) {
	application_t app;
	memset(&app, 0, sizeof(app));
	app.name = string_const(STRING_CONST("Memory alloc tests"));
	app.short_name = string_const(STRING_CONST("test_alloc"));
	app.company = string_const(STRING_CONST(""));
	app.flags = APPLICATION_UTILITY;
	app.exception_handler = test_exception_handler;
	return app;
}

static foundation_config_t
test_alloc_config(void) {
	foundation_config_t config;
	memset(&config, 0, sizeof(config));
	return config;
}

static memory_system_t
test_alloc_memory_system(void) {
	return memory_system_malloc();
}

static int
test_alloc_initialize(void) {
	log_set_suppress(HASH_MEMORY, ERRORLEVEL_DEBUG);
	return 0;
}

static void
test_alloc_finalize(void) {
}

DECLARE_TEST(alloc, alloc) {
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int icheck = 0;
	unsigned int id = 0;
	void* addr[8142];
	char data[20000];
	unsigned int datasize[7] = {473, 39, 195, 24, 73, 376, 245};

	memory_system_t memsys = memory_system();

	memsys.initialize();
	memsys.thread_initialize();
	memsys.thread_finalize();
	memsys.finalize();

	memsys.initialize();
	memsys.thread_initialize();

	for (id = 0; id < 20000; ++id)
		data[id] = (char)(id % 139 + id % 17);

	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 8142; ++ipass) {
			addr[ipass] = memsys.allocate(0, 500, 0, MEMORY_PERSISTENT);
			EXPECT_NE(addr[ipass], 0);

			memcpy(addr[ipass], data, 500);

			for (icheck = 0; icheck < ipass; ++icheck) {
				EXPECT_NE(addr[icheck], addr[ipass]);
				if (addr[icheck] < addr[ipass])
					EXPECT_LE(pointer_offset(addr[icheck], 500), addr[ipass]);
				else if (addr[icheck] > addr[ipass])
					EXPECT_LE(pointer_offset(addr[ipass], 500), addr[icheck]);
			}
		}

		for (ipass = 0; ipass < 8142; ++ipass)
			EXPECT_EQ(memcmp(addr[ipass], data, 500), 0);

		for (ipass = 0; ipass < 8142; ++ipass)
			memsys.deallocate(addr[ipass]);
	}

	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 1024; ++ipass) {
			unsigned int cursize = datasize[ipass % 7] + ipass;

			addr[ipass] = memsys.allocate(0, cursize, 0, MEMORY_PERSISTENT);
			EXPECT_NE(addr[ipass], 0);

			memcpy(addr[ipass], data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				EXPECT_NE(addr[icheck], addr[ipass]);
				/*if( addr[icheck] < addr[ipass] )
				    EXPECT_LE( pointer_offset( addr[icheck], cursize ), addr[ipass] );
				else if( addr[icheck] > addr[ipass] )
				    EXPECT_LE( pointer_offset( addr[ipass], cursize ), addr[icheck] );*/
			}
		}

		for (ipass = 0; ipass < 1024; ++ipass) {
			unsigned int cursize = datasize[ipass % 7] + ipass;
			EXPECT_EQ(memcmp(addr[ipass], data, cursize), 0);
		}

		for (ipass = 0; ipass < 1024; ++ipass)
			memsys.deallocate(addr[ipass]);
	}

	for (iloop = 0; iloop < 128; ++iloop) {
		for (ipass = 0; ipass < 1024; ++ipass) {
			addr[ipass] = memsys.allocate(0, 500, 0, MEMORY_PERSISTENT);
			EXPECT_NE(addr[ipass], 0);

			memcpy(addr[ipass], data, 500);

			for (icheck = 0; icheck < ipass; ++icheck) {
				EXPECT_NE(addr[icheck], addr[ipass]);
				if (addr[icheck] < addr[ipass])
					EXPECT_LE(pointer_offset(addr[icheck], 500), addr[ipass]);
				else if (addr[icheck] > addr[ipass])
					EXPECT_LE(pointer_offset(addr[ipass], 500), addr[icheck]);
			}
		}

		for (ipass = 0; ipass < 1024; ++ipass) {
			EXPECT_EQ(memcmp(addr[ipass], data, 500), 0);
		}

		for (ipass = 0; ipass < 1024; ++ipass)
			memsys.deallocate(addr[ipass]);
	}

	memsys.thread_finalize();
	memsys.finalize();

	return 0;
}

typedef struct _allocator_thread_arg {
	memory_system_t memory_system;
	unsigned int loops;
	unsigned int passes;  // max 4096
	unsigned int datasize[32];
	unsigned int num_datasize;  // max 32
	void** pointers;
} allocator_thread_arg_t;

static void*
allocator_thread(void* argp) {
	allocator_thread_arg_t arg = *(allocator_thread_arg_t*)argp;
	memory_system_t memsys = arg.memory_system;
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int icheck = 0;
	unsigned int id = 0;
	void* addr[4096];
	char data[8192];
	unsigned int cursize;
	unsigned int iwait = 0;

	memsys.thread_initialize();

	for (id = 0; id < 8192; ++id)
		data[id] = (char)id;

	iwait = random32_range(0, 10);
	thread_sleep(iwait);

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = 4 + arg.datasize[(iloop + ipass + iwait) % arg.num_datasize] + ((iloop + ipass) % 1024);

			addr[ipass] = memsys.allocate(0, 4 + cursize, 0, MEMORY_PERSISTENT);
			EXPECT_NE(addr[ipass], 0);

			*(uint32_t*)addr[ipass] = (uint32_t)cursize;
			memcpy(pointer_offset(addr[ipass], 4), data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				EXPECT_NE(addr[icheck], addr[ipass]);
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], *(uint32_t*)addr[icheck]) > addr[ipass])
						EXPECT_LE(pointer_offset(addr[icheck], *(uint32_t*)addr[icheck]), addr[ipass]);
				} else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], *(uint32_t*)addr[ipass]) > addr[ipass])
						EXPECT_LE(pointer_offset(addr[ipass], *(uint32_t*)addr[ipass]), addr[icheck]);
				}
			}
		}

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = *(uint32_t*)addr[ipass];

			EXPECT_EQ(memcmp(pointer_offset(addr[ipass], 4), data, cursize), 0);
			memsys.deallocate(addr[ipass]);
		}
	}

	memsys.thread_finalize();

	return 0;
}

DECLARE_TEST(alloc, threaded) {
	thread_t thread[32];
	void* threadres[32];
	unsigned int i;
	size_t num_alloc_threads;
	allocator_thread_arg_t thread_arg;
	memory_system_t memsys = memory_system();
	memsys.initialize();
	memsys.thread_initialize();

	num_alloc_threads = system_hardware_threads();
	if (num_alloc_threads < 3)
		num_alloc_threads = 3;
	if (num_alloc_threads > 32)
		num_alloc_threads = 32;

	// Warm-up
	thread_arg.memory_system = memsys;
	thread_arg.loops = 2000;
	thread_arg.passes = 512;
	thread_arg.datasize[0] = 19;
	thread_arg.datasize[1] = 249;
	thread_arg.datasize[2] = 797;
	thread_arg.datasize[3] = 3;
	thread_arg.datasize[4] = 79;
	thread_arg.datasize[5] = 34;
	thread_arg.datasize[6] = 389;
	thread_arg.num_datasize = 7;

	EXPECT_EQ(allocator_thread(&thread_arg), 0);

	for (i = 0; i < 7; ++i)
		thread_arg.datasize[i] = 500;
	EXPECT_EQ(allocator_thread(&thread_arg), 0);

	thread_arg.datasize[0] = 19;
	thread_arg.datasize[1] = 249;
	thread_arg.datasize[2] = 797;
	thread_arg.datasize[3] = 3;
	thread_arg.datasize[4] = 79;
	thread_arg.datasize[5] = 34;
	thread_arg.datasize[6] = 389;
	thread_arg.num_datasize = 7;

	for (i = 0; i < num_alloc_threads; ++i) {
		thread_initialize(thread + i, allocator_thread, &thread_arg, STRING_CONST("allocator"), THREAD_PRIORITY_NORMAL,
		                  0);
		thread_start(thread + i);
	}

	test_wait_for_threads_startup(thread, num_alloc_threads);
	test_wait_for_threads_finish(thread, num_alloc_threads);

	for (i = 0; i < num_alloc_threads; ++i) {
		threadres[i] = thread_join(thread + i);
		thread_finalize(thread + i);
	}

	memsys.thread_finalize();
	memsys.finalize();

	for (i = 0; i < num_alloc_threads; ++i)
		EXPECT_EQ(threadres[i], 0);

	return 0;
}

static void*
crossallocator_thread(void* argp) {
	allocator_thread_arg_t arg = *(allocator_thread_arg_t*)argp;
	memory_system_t memsys = arg.memory_system;
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int cursize;
	unsigned int iwait = 0;

	memsys.thread_initialize();

	iwait = random32_range(0, 10);
	thread_sleep(iwait);

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = arg.datasize[(iloop + ipass + iwait) % arg.num_datasize] + (iloop % 1024);

			void* addr = memsys.allocate(0, cursize, 0, MEMORY_PERSISTENT);
			EXPECT_NE(addr, 0);

			arg.pointers[iloop * arg.passes + ipass] = addr;
		}
	}

	memsys.thread_finalize();

	return 0;
}

DECLARE_TEST(alloc, crossthread) {
	thread_t thread;
	allocator_thread_arg_t thread_arg;

	memory_system_t memsys = memory_system();
	memsys.initialize();
	memsys.thread_initialize();

	thread_arg.memory_system = memsys;
	thread_arg.loops = 100;
	thread_arg.passes = 1024;
	thread_arg.pointers =
	    memory_allocate(HASH_TEST, sizeof(void*) * thread_arg.loops * thread_arg.passes, 0, MEMORY_PERSISTENT);
	thread_arg.datasize[0] = 19;
	thread_arg.datasize[1] = 249;
	thread_arg.datasize[2] = 797;
	thread_arg.datasize[3] = 3;
	thread_arg.datasize[4] = 79;
	thread_arg.datasize[5] = 34;
	thread_arg.datasize[6] = 389;
	thread_arg.num_datasize = 7;

	thread_initialize(&thread, crossallocator_thread, &thread_arg, STRING_CONST("crossallocator"),
	                  THREAD_PRIORITY_NORMAL, 0);
	thread_start(&thread);

	test_wait_for_threads_startup(&thread, 1);
	test_wait_for_threads_finish(&thread, 1);

	EXPECT_EQ(thread_join(&thread), 0);
	thread_finalize(&thread);

	// Off-thread deallocation
	for (size_t iptr = 0; iptr < thread_arg.loops * thread_arg.passes; ++iptr)
		memsys.deallocate(thread_arg.pointers[iptr]);

	memory_deallocate(thread_arg.pointers);

	// Simulate thread exit
	memsys.thread_finalize();

	memsys.finalize();

	return 0;
}

static void*
initfini_thread(void* argp) {
	allocator_thread_arg_t arg = *(allocator_thread_arg_t*)argp;
	memory_system_t memsys = arg.memory_system;
	unsigned int iloop = 0;
	unsigned int ipass = 0;
	unsigned int icheck = 0;
	unsigned int id = 0;
	void* addr[4096];
	char data[8192];
	unsigned int cursize;
	unsigned int iwait = 0;

	for (id = 0; id < 8192; ++id)
		data[id] = (char)id;

	thread_yield();

	for (iloop = 0; iloop < arg.loops; ++iloop) {
		memsys.thread_initialize();

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = 4 + arg.datasize[(iloop + ipass + iwait) % arg.num_datasize] + (iloop % 1024);

			addr[ipass] = memsys.allocate(0, 4 + cursize, 0, MEMORY_PERSISTENT);
			EXPECT_NE(addr[ipass], 0);

			*(uint32_t*)addr[ipass] = (uint32_t)cursize;
			memcpy(pointer_offset(addr[ipass], 4), data, cursize);

			for (icheck = 0; icheck < ipass; ++icheck) {
				EXPECT_NE(addr[icheck], addr[ipass]);
				if (addr[icheck] < addr[ipass]) {
					if (pointer_offset(addr[icheck], *(uint32_t*)addr[icheck]) > addr[ipass])
						EXPECT_LE(pointer_offset(addr[icheck], *(uint32_t*)addr[icheck]), addr[ipass]);
				} else if (addr[icheck] > addr[ipass]) {
					if (pointer_offset(addr[ipass], *(uint32_t*)addr[ipass]) > addr[ipass])
						EXPECT_LE(pointer_offset(addr[ipass], *(uint32_t*)addr[ipass]), addr[icheck]);
				}
			}
		}

		for (ipass = 0; ipass < arg.passes; ++ipass) {
			cursize = *(uint32_t*)addr[ipass];

			EXPECT_EQ(memcmp(pointer_offset(addr[ipass], 4), data, cursize), 0);
			memsys.deallocate(addr[ipass]);
		}

		memsys.thread_finalize();
	}

	return 0;
}

DECLARE_TEST(alloc, threadspam) {
	thread_t thread[64];
	void* threadres[64];
	unsigned int i, j;
	size_t num_passes, num_alloc_threads;
	allocator_thread_arg_t thread_arg;
	memory_system_t memsys = memory_system();
	memsys.initialize();
	memsys.thread_initialize();

	num_passes = 1000;
	num_alloc_threads = (system_hardware_threads() * 2) + 1;
	if (num_alloc_threads < 4)
		num_alloc_threads = 4;
	if (num_alloc_threads > 64)
		num_alloc_threads = 64;

	// Warm-up
	thread_arg.memory_system = memsys;
	thread_arg.loops = 100;
	thread_arg.passes = 10;
	thread_arg.datasize[0] = 19;
	thread_arg.datasize[1] = 249;
	thread_arg.datasize[2] = 797;
	thread_arg.datasize[3] = 3;
	thread_arg.datasize[4] = 79;
	thread_arg.datasize[5] = 34;
	thread_arg.datasize[6] = 389;
	thread_arg.num_datasize = 7;

	EXPECT_EQ(allocator_thread(&thread_arg), 0);

	for (i = 0; i < 7; ++i)
		thread_arg.datasize[i] = 500;
	EXPECT_EQ(allocator_thread(&thread_arg), 0);

	thread_arg.datasize[0] = 19;
	thread_arg.datasize[1] = 249;
	thread_arg.datasize[2] = 797;
	thread_arg.datasize[3] = 3;
	thread_arg.datasize[4] = 79;
	thread_arg.datasize[5] = 34;
	thread_arg.datasize[6] = 389;
	thread_arg.num_datasize = 7;

	for (i = 0; i < num_alloc_threads; ++i) {
		thread_initialize(thread + i, initfini_thread, &thread_arg, STRING_CONST("allocator"), THREAD_PRIORITY_NORMAL,
		                  0);
		thread_start(thread + i);
	}

	test_wait_for_threads_startup(thread, num_alloc_threads);

	for (j = 0; j < num_passes; ++j) {
		thread_sleep(1);

		atomic_thread_fence_acquire();

		for (i = 0; i < num_alloc_threads; ++i) {
			while (!thread_is_started(thread + i))
				thread_yield();
			threadres[i] = thread_join(thread + i);
			thread_finalize(thread + i);
			if (threadres[i])
				break;
			thread_initialize(thread + i, initfini_thread, &thread_arg, STRING_CONST("allocator"),
			                  THREAD_PRIORITY_NORMAL, 0);
			thread_start(thread + i);
		}
	}

	test_wait_for_threads_finish(thread, num_alloc_threads);

	for (i = 0; i < num_alloc_threads; ++i) {
		if (!threadres[i]) {
			threadres[i] = thread_join(thread + i);
			thread_finalize(thread + i);
		}
	}

	memsys.thread_finalize();
	memsys.finalize();

	for (i = 0; i < num_alloc_threads; ++i)
		EXPECT_EQ(threadres[i], 0);

	return 0;
}

static void
test_alloc_declare(void) {
	ADD_TEST(alloc, alloc);
	ADD_TEST(alloc, threaded);
	ADD_TEST(alloc, crossthread);
	ADD_TEST(alloc, threadspam);
}

static test_suite_t test_alloc_suite = {test_alloc_application,
                                        test_alloc_memory_system,
                                        test_alloc_config,
                                        test_alloc_declare,
                                        test_alloc_initialize,
                                        test_alloc_finalize,
                                        0};

#if BUILD_MONOLITHIC

int
test_alloc_run(void);

int
test_alloc_run(void) {
	test_suite = test_alloc_suite;
	return test_run_all();
}

#else

test_suite_t
test_suite_define(void);

test_suite_t
test_suite_define(void) {
	return test_alloc_suite;
}

#endif
