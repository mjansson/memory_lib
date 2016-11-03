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
#include <memory/log.h>

typedef struct _benchmark_result {
	tick_t  elapsed;
	size_t  ops;
} benchmark_result_t;

typedef benchmark_result_t (*benchmark_loop_fn)(memory_system_t* memsys, void** ptr,
                                                size_t* size);

typedef struct _benchmark_arg {
	benchmark_loop_fn   function;
	benchmark_result_t  res;
	memory_system_t*    memsys;
	void**              ptr;
	size_t*             size;
} benchmark_arg_t;

static void** ptr_malloc[64];
static void** ptr_memory[64];
static size_t* random_size;

static void*
benchmark_thread(void* argp) {
	int iloop;
	benchmark_arg_t* arg = argp;

	thread_sleep(500);

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
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->allocate(0, (size_t)(ipass + iloop), 0, MEMORY_PERSISTENT);
			++res.ops;
		}
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
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->allocate(0, size[ ipass ], 0, MEMORY_PERSISTENT);
			++res.ops;
		}
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
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr[ipass] = memsys->reallocate(ptr[ipass], size[(ipass * iloop) % 8192 ], 0, size[ ipass ]);
			res.ops++;
		}
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
		for (ipass = 0; ipass < 8192; ++ipass) {
			memsys->deallocate(ptr[ipass]);
			res.ops++;
		}
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

	if ((ret = foundation_initialize(memory_system_malloc(), app, config)) < 0)
		return ret;

	return 0;
}

int
main_run(void* main_arg) {
	int iloop, ipass;// , iseq;
	unsigned int i;
	benchmark_result_t res, res_worst, res_best;
	thread_t thread[64];
	benchmark_arg_t arg[64];
	size_t num_thread;

	FOUNDATION_UNUSED(main_arg);

	memory_system_t sys_malloc = memory_system_malloc();
	memory_system_t sys_memory = memory_system();

	sys_malloc.initialize();
	sys_memory.initialize();

	//Warmup
	for (iloop = 0; iloop < 64; ++iloop) {
		ptr_malloc[iloop] = sys_malloc.allocate(0, (size_t)(iloop+1), 0, MEMORY_PERSISTENT);
		ptr_memory[iloop] = sys_memory.allocate(0, (size_t)(iloop+1), 0, MEMORY_PERSISTENT);
		sys_malloc.deallocate(ptr_malloc[iloop]);
		sys_memory.deallocate(ptr_memory[iloop]);

		ptr_malloc[iloop] = sys_malloc.allocate(0, sizeof(void*) * (size_t)iloop, 0, MEMORY_PERSISTENT);
		ptr_memory[iloop] = sys_memory.allocate(0, sizeof(void*) * (size_t)iloop, 0, MEMORY_PERSISTENT);
		sys_malloc.deallocate(ptr_malloc[iloop]);
		sys_memory.deallocate(ptr_memory[iloop]);

		ptr_malloc[iloop] = sys_malloc.allocate(0, 1024 * (size_t)iloop, 0, MEMORY_PERSISTENT);
		ptr_memory[iloop] = sys_memory.allocate(0, 1024 * (size_t)iloop, 0, MEMORY_PERSISTENT);
		sys_malloc.deallocate(ptr_malloc[iloop]);
		sys_memory.deallocate(ptr_memory[iloop]);
	}

	for (iloop = 0; iloop < 64; ++iloop) {
		ptr_malloc[iloop] = sys_malloc.allocate(0, sizeof(void*) * 8192, 0, MEMORY_PERSISTENT);
		ptr_memory[iloop] = sys_memory.allocate(0, sizeof(void*) * 8192, 0, MEMORY_PERSISTENT);
	}

	random_size = memory_allocate(0, sizeof(size_t) * 8192, 0, MEMORY_PERSISTENT);
	for (iloop = 0; iloop < 8192; ++iloop)
		random_size[iloop] = random32_range(0, 8192);

	num_thread = system_hardware_threads() * 2;
	if (num_thread < 3)
		num_thread = 3;
	if (num_thread > 64)
		num_thread = 64;

	//Warmup phase
	log_info(HASH_BENCHMARK, STRING_CONST("Benchmark initializing"));
	for (iloop = 0; iloop < 64; ++iloop) {
		for (ipass = 0; ipass < 8192; ++ipass) {
			ptr_malloc[0][ipass] = sys_malloc.allocate(0, (size_t)(ipass + iloop), 0, MEMORY_PERSISTENT);
			ptr_memory[0][ipass] = sys_memory.allocate(0, (size_t)(ipass + iloop), 0, MEMORY_PERSISTENT);
		}

		for (ipass = 0; ipass < 8192; ++ipass) {
			sys_malloc.deallocate(ptr_malloc[0][ipass]);
			sys_memory.deallocate(ptr_memory[0][ipass]);
		}
	}

	log_infof(HASH_BENCHMARK, STRING_CONST("Running on %" PRIsize " cores"), system_hardware_threads());

	/*log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Single threaded sequential small allocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("==========================================="));
	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_allocation_loop(&sys_malloc, ptr_malloc[0], random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc time: %.4" PRIreal "s : %u allocs/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_allocation_loop(&sys_memory, ptr_memory[0], random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory time: %.4" PRIreal "s : %u allocs/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Single threaded random small allocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("======================================="));
	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_random_allocation_loop(&sys_malloc, ptr_malloc[0],
		                             random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc time: %.4" PRIreal "s : %u allocs/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_random_allocation_loop(&sys_memory, ptr_memory[0],
		                             random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory time: %.4" PRIreal "s : %u allocs/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Single threaded random reallocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("==================================="));
	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_random_reallocation_loop(&sys_malloc, ptr_malloc[0],
		                             random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc time: %.4" PRIreal "s : %u reallocs/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_random_reallocation_loop(&sys_memory, ptr_memory[0],
		                             random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory time: %.4" PRIreal "s : %u reallocs/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Single threaded random deallocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("==================================="));
	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_random_deallocation_loop(&sys_malloc, ptr_malloc[0],
		                             random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc time: %.4" PRIreal "s : %u deallocs/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_random_deallocation_loop(&sys_memory, ptr_memory[0],
		                             random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory time: %.4" PRIreal "s : %u deallocs/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK,
	         STRING_CONST("Single threaded mixed allocation/reallocation/deallocation"));
	log_info(HASH_BENCHMARK,
	         STRING_CONST("=========================================================="));
	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_random_mixed_loop(&sys_malloc, ptr_malloc[0], random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc time: %.4" PRIreal "s : %u ops/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));

	res.elapsed = 0;
	res.ops= 0;
	for (iseq = 0; iseq < 16; ++iseq) {
		benchmark_result_t current = _run_small_random_mixed_loop(&sys_memory, ptr_memory[0], random_size);
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory time: %.4" PRIreal "s : %u ops/s"),
	          time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)));*/

	log_info(HASH_BENCHMARK, STRING_CONST(""));
	log_info(HASH_BENCHMARK, STRING_CONST("Multi threaded sequential small allocation"));
	log_info(HASH_BENCHMARK, STRING_CONST("=========================================="));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_allocation_loop;
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_allocation_loop;
		arg[i].memsys = &sys_memory;
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
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory avg time: %.4" PRIreal "s : %u ops/s (best %.4"
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
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_random_allocation_loop;
		arg[i].memsys = &sys_memory;
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
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory avg time: %.4" PRIreal "s : %u ops/s (best %.4"
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
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_random_reallocation_loop;
		arg[i].memsys = &sys_memory;
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
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory avg time: %.4" PRIreal "s : %u ops/s (best %.4"
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
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc avg time: %.4fs : %u ops/s (best %.4fs, worst %.4fs)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_random_deallocation_loop;
		arg[i].memsys = &sys_memory;
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
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory avg time: %.4" PRIreal "s : %u ops/s (best %.4"
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
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread_initialize(thread + i, benchmark_thread, arg + i, STRING_CONST("allocator"),
		                  THREAD_PRIORITY_NORMAL, 0);
	}

	for (i = 0; i < num_thread; ++i)
		thread_start(thread + i);
	_collect_thread_results(thread, arg, num_thread, &res, &res_worst, &res_best);
	for (i = 0; i < num_thread; ++i)
		thread_finalize(thread + i);
	log_infof(HASH_BENCHMARK, STRING_CONST("Malloc avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	for (i = 0; i < num_thread; ++i) {
		memset(arg + i, 0, sizeof(benchmark_arg_t));

		arg[i].function = _run_small_random_mixed_loop;
		arg[i].memsys = &sys_memory;
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
	log_infof(HASH_BENCHMARK, STRING_CONST("Memory avg time: %.4" PRIreal "s : %u ops/s (best %.4"
	                                       PRIreal "s, worst %.4" PRIreal "s)"),
	          (double)time_ticks_to_seconds(res.elapsed),
	          (unsigned int)((real)res.ops / time_ticks_to_seconds(res.elapsed)),
	          (double)time_ticks_to_seconds(res_best.elapsed),
	          (double)time_ticks_to_seconds(res_worst.elapsed));

	for (iloop = 0; iloop < 64; ++iloop) {
		sys_malloc.deallocate(ptr_malloc[iloop]);
		sys_memory.deallocate(ptr_memory[iloop]);
	}

	memory_deallocate(random_size);

	sys_malloc.finalize();
	sys_memory.finalize();

	return 0;
}

void
main_finalize(void) {
	foundation_finalize();
}
