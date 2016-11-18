/* main.c  -  Memory allocation tests  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a cross-platform memory allocation library in C11 providing basic support data types and
 * functions to write applications and games in a platform-independent fashion. The latest source code is
 * always available at
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

#include <memory/memory.h>

static memory_system_t _memory_system_to_test;
static size_t _num_threads_to_test;

typedef struct _benchmark_result {
	tick_t  elapsed;
	size_t  ops;
} benchmark_result_t;

typedef benchmark_result_t (*benchmark_loop_fn)(
    memory_system_t* memsys, void** ptr, size_t* size);

typedef struct _benchmark_arg {
	benchmark_loop_fn   function;
	benchmark_result_t  res;
	memory_system_t*    memsys;
	void**              ptr;
	size_t*             size;
} benchmark_arg_t;

static void** ptr_memory[64];
static size_t* random_size;


static void
_run_thread_warmup(memory_system_t* memsys) {
	size_t iloop, stepsize, loopsteps, iblock, blocksteps;
	volatile uintptr_t result = 0;
	void** ptrs;

	stepsize = 16;
	loopsteps = 65536 / stepsize;
	blocksteps = 256;

	ptrs = memsys->allocate(0, sizeof(void*) * loopsteps * blocksteps, 0, MEMORY_PERSISTENT);
	for (iloop = 0; iloop < loopsteps; ++iloop) {
		for (iblock = 0; iblock < blocksteps; ++iblock) {
			ptrs[iloop*blocksteps + iblock] = memsys->allocate(0, iloop * stepsize, 0, MEMORY_PERSISTENT);
			result += (uintptr_t)ptrs[iloop*blocksteps + iblock];
		}

		for (iblock = 0; iblock < blocksteps; ++iblock) {
			memsys->deallocate(ptrs[iloop*blocksteps + iblock]);
		}
	}

	memsys->deallocate(ptrs);
}

static void*
benchmark_thread(void* argp) {
	int iloop;
	benchmark_arg_t* arg = argp;

	thread_sleep(100);

	_run_thread_warmup(arg->memsys);

	for (iloop = 0; iloop < 8; ++iloop) {
		benchmark_result_t current = arg->function(arg->memsys, arg->ptr, arg->size);
		arg->res.elapsed += current.elapsed;
		arg->res.ops += current.ops;
	}

	return 0;
}

static benchmark_result_t
_run_small_allocation_loop(memory_system_t* memsys, void** ptr, size_t* size) {
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res;

	FOUNDATION_UNUSED(size);

	memset(&res, 0, sizeof(res));
	for (iloop = 0; iloop < 512; ++iloop) {
		time_start = time_current();
		atomic_thread_fence_sequentially_consistent();
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->allocate(0, (size_t)(ipass + iloop), 0, MEMORY_PERSISTENT);
			++res.ops;
		}
		atomic_thread_fence_sequentially_consistent();
		time_end = time_current();
		res.elapsed += time_diff(time_start, time_end);
		for (ipass = 0; ipass < 8192; ++ipass) {
			memsys->deallocate(ptr[ipass]);
		}
	}
	return res;
}

static benchmark_result_t
_run_small_random_allocation_loop(memory_system_t* memsys, void** ptr, size_t* size) {
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res;

	memset(&res, 0, sizeof(res));
	for (iloop = 0; iloop < 512; ++iloop) {
		time_start = time_current();
		atomic_thread_fence_sequentially_consistent();
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->allocate(0, size[ ipass ], 0, MEMORY_PERSISTENT);
			++res.ops;
		}
		atomic_thread_fence_sequentially_consistent();
		time_end = time_current();
		res.elapsed += time_diff(time_start, time_end);
		for (ipass = 0; ipass < 8192; ++ipass) {
			memsys->deallocate(ptr[ipass]);
		}
	}
	return res;
}

static benchmark_result_t
_run_small_random_reallocation_loop(memory_system_t* memsys, void** ptr, size_t* size) {
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res;

	memset(&res, 0, sizeof(res));
	for (iloop = 0; iloop < 512; ++iloop) {
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->allocate(0, size[ipass], 0, MEMORY_PERSISTENT);
		}
		time_start = time_current();
		atomic_thread_fence_sequentially_consistent();
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->reallocate(ptr[ipass], size[(ipass * iloop) % 8192 ], 0, size[ ipass ]);
			res.ops++;
		}
		atomic_thread_fence_sequentially_consistent();
		time_end = time_current();
		res.elapsed += time_diff(time_start, time_end);
		for (ipass = 0; ipass < 8192; ++ipass) {
			memsys->deallocate(ptr[ipass]);
		}
	}
	return res;
}

static benchmark_result_t
_run_small_random_deallocation_loop(memory_system_t* memsys, void** ptr, size_t* size) {
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res;

	memset(&res, 0, sizeof(res));
	for (iloop = 0; iloop < 512; ++iloop) {
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->allocate(0, size[ ipass ], 0, MEMORY_PERSISTENT);
		}
		time_start = time_current();
		atomic_thread_fence_sequentially_consistent();
		for (ipass = 0; ipass < 8192; ++ipass) {
			memsys->deallocate(ptr[ipass]);
			res.ops++;
		}
		atomic_thread_fence_sequentially_consistent();
		time_end = time_current();
		res.elapsed += time_diff(time_start, time_end);
	}
	return res;
}

static benchmark_result_t
_run_small_random_mixed_loop(memory_system_t* memsys, void** ptr, size_t* size) {
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res;

	memset(&res, 0, sizeof(res));
	for (iloop = 0; iloop < 512; ++iloop) {
		time_start = time_current();
		atomic_thread_fence_sequentially_consistent();
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->allocate(0, size[ ipass ], 0, MEMORY_PERSISTENT);
			++res.ops;
			if ((ipass % 3) == 1) {
				ptr[ipass] = memsys->reallocate(ptr[ipass], size[(ipass * iloop + 1) % 8192 ], 0, size[ ipass ]);
				++res.ops;
			}
			else if ((ipass % 3) == 2) {
				memsys->deallocate(ptr[ipass]);
				ptr[ipass] = memsys->allocate(0, size[(ipass * iloop + 2) % 8192 ], 0, MEMORY_PERSISTENT);
				res.ops += 2;
			}
		}
		for (ipass = 0; ipass < 8192; ++ipass) {
			memsys->deallocate(ptr[ipass]);
			++res.ops;
		}
		atomic_thread_fence_sequentially_consistent();
		time_end = time_current();
		res.elapsed += time_diff(time_start, time_end);
	}
	return res;
}

static void
_collect_thread_results(thread_t* thread, benchmark_arg_t* arg, size_t num_thread,
                        benchmark_result_t* res_avg, benchmark_result_t* res_worst, benchmark_result_t* res_best) {
	size_t i;
	bool wait;

	do {
		wait = false;
		for (i = 0; i < num_thread; ++i) {
			if (!thread_is_started(thread  + i))
				wait = true;
		}
		thread_sleep(100);
	}
	while (wait);

	do {
		wait = false;
		for (i = 0; i < num_thread; ++i) {
			if (thread_is_running(thread + i))
				wait = true;
		}
		thread_sleep(100);
	}
	while (wait);

	thread_sleep(100);

	res_avg->elapsed = 0;
	res_avg->ops = 0;

	res_worst->elapsed = arg[0].res.elapsed;
	res_worst->ops = arg[0].res.ops;

	res_best->elapsed = arg[0].res.elapsed;
	res_best->ops = arg[0].res.ops;

	for (i = 0; i < num_thread; ++i) {
		res_avg->elapsed += arg[i].res.elapsed;
		res_avg->ops += arg[i].res.ops;

		if (arg[i].res.elapsed > res_worst->elapsed) {
			res_worst->elapsed = arg[i].res.elapsed;
			res_worst->ops = arg[i].res.ops;
		}

		if (arg[i].res.elapsed < res_best->elapsed) {
			res_best->elapsed = arg[i].res.elapsed;
			res_best->ops = arg[i].res.ops;
		}
	}
	res_avg->elapsed /= num_thread;
	res_avg->ops /= num_thread;
}

int
main_initialize(void) {
	int ret = 0;

	application_t app;
	memset(&app, 0, sizeof(app));
	app.name = string_const(STRING_CONST("Memory allocation benchmark"));
	app.short_name = string_const(STRING_CONST("benchmark_alloc"));
	app.company = string_const(STRING_CONST("Rampant Pixels"));
	app.flags = APPLICATION_UTILITY;

	foundation_config_t config;
	memset(&config, 0, sizeof(config));

	log_enable_prefix(false);
	//log_set_suppress(0, ERRORLEVEL_INFO);
	//log_set_suppress(HASH_MEMORY, ERRORLEVEL_INFO);
	//log_set_suppress(HASH_BENCHMARK, ERRORLEVEL_DEBUG);

	_memory_system_to_test = memory_system();
	//_memory_system_to_test = memory_system_malloc();
	//_memory_system_to_test = memory_system_ptmalloc3();
	//_memory_system_to_test = memory_system_nedmalloc();
	//_memory_system_to_test = memory_system_tcmalloc();

	if ((ret = foundation_initialize(_memory_system_to_test, app, config)) < 0)
		return ret;

	_num_threads_to_test = system_hardware_threads() + 1;

	return 0;
}

int
main_run(void* main_arg) {
	int iloop;
	unsigned int i;
	benchmark_result_t res, res_worst, res_best;
	thread_t thread[64];
	benchmark_arg_t arg[64];
	size_t num_thread;

	FOUNDATION_UNUSED(main_arg);

	//Allocate memory for holding the allocated blocks
	for (iloop = 0; iloop < 64; ++iloop) {
		ptr_memory[iloop] = _memory_system_to_test.allocate(0, sizeof(void*) * 8192, 0, MEMORY_PERSISTENT);
	}

	random_size = memory_allocate(0, sizeof(size_t) * 8192, 0, MEMORY_PERSISTENT);
	for (iloop = 0; iloop < 8192; ++iloop)
		random_size[iloop] = random32_range(0, 8192);

	num_thread = _num_threads_to_test;

	//Warmup phase
	log_infof(HASH_BENCHMARK, STRING_CONST("Benchmark initializing, running on %" PRIsize
	                                       " cores with %" PRIsize " threads"),
	          system_hardware_threads(), num_thread);

	_run_thread_warmup(&_memory_system_to_test);

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Multi threaded sequential small allocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("=========================================="));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_allocation_loop;
		arg[i].memsys = &_memory_system_to_test;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Multi threaded random small allocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("======================================"));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_random_allocation_loop;
		arg[i].memsys = &_memory_system_to_test;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Multi threaded random reallocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("=================================="));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_random_reallocation_loop;
		arg[i].memsys = &_memory_system_to_test;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Multi threaded random deallocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("=================================="));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_random_deallocation_loop;
		arg[i].memsys = &_memory_system_to_test;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK,
	         STRING_CONST("Multi threaded random mixed allocation/reallocation/deallocation"));
	log_info(HASH_BENCHMARK,
	         STRING_CONST("================================================================"));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_random_mixed_loop;
		arg[i].memsys = &_memory_system_to_test;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	for (iloop = 0; iloop < 64; ++iloop) {
		_memory_system_to_test.deallocate(ptr_memory[iloop]);
	}

	memory_deallocate(random_size);

	return 0;
}

void
main_finalize(void) {
	foundation_finalize();
}
