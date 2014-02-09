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


typedef struct _benchmark_result
{
	tick_t    elapsed;
	uint32_t  ops;
} benchmark_result_t;

typedef benchmark_result_t (*benchmark_loop_fn)( memory_system_t* memsys, void** ptr, uint32_t* size );

typedef struct _benchmark_arg
{
	benchmark_loop_fn   function;
	benchmark_result_t  res;
	memory_system_t*    memsys;
	void**              ptr;
	uint32_t*           size;
} benchmark_arg_t;

static void** ptr_malloc[64];
static void** ptr_memory[64];
static uint32_t* random_size;


static void* benchmark_thread( object_t thread, void* argp )
{
	int iloop;
	benchmark_arg_t* arg = argp;
	
	thread_sleep( 500 );

	for( iloop = 0; iloop < 8; ++iloop )
	{
		benchmark_result_t current = arg->function( arg->memsys, arg->ptr, arg->size );
		arg->res.elapsed += current.elapsed;
		arg->res.ops += current.ops;
	}

	return 0;
}


static benchmark_result_t _run_small_allocation_loop( memory_system_t* memsys, void** ptr, uint32_t* size )
{
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res = {0};

	for( iloop = 0; iloop < 512; ++iloop )
	{
		time_start = time_current();
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			ptr[ipass] = memsys->allocate( 0, ( ipass + iloop ), 0, MEMORY_PERSISTENT );
			++res.ops;
		}
		time_end = time_current();
		res.elapsed += time_diff( time_start, time_end );
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			memsys->deallocate( ptr[ipass] );
		}
	}
	return res;
}


static benchmark_result_t _run_small_random_allocation_loop( memory_system_t* memsys, void** ptr, uint32_t* size )
{
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res = {0};

	for( iloop = 0; iloop < 512; ++iloop )
	{
		time_start = time_current();
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			ptr[ipass] = memsys->allocate( 0, size[ ipass ], 0, MEMORY_PERSISTENT );
			++res.ops;
		}
		time_end = time_current();
		res.elapsed += time_diff( time_start, time_end );
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			memsys->deallocate( ptr[ipass] );
		}
	}
	return res;
}


static benchmark_result_t _run_small_random_reallocation_loop( memory_system_t* memsys, void** ptr, uint32_t* size )
{
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res = {0};

	for( iloop = 0; iloop < 512; ++iloop )
	{
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			ptr[ipass] = memsys->allocate( 0, size[ ipass ], 0, MEMORY_PERSISTENT );
		}
		time_start = time_current();
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			ptr[ipass] = memsys->reallocate( ptr[ipass], size[ ( ipass * iloop ) % 8192 ], 0, size[ ipass ] );
			res.ops++;
		}
		time_end = time_current();
		res.elapsed += time_diff( time_start, time_end );
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			memsys->deallocate( ptr[ipass] );
		}
	}
	return res;
}


static benchmark_result_t _run_small_random_deallocation_loop( memory_system_t* memsys, void** ptr, uint32_t* size )
{
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res = {0};

	for( iloop = 0; iloop < 512; ++iloop )
	{
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			ptr[ipass] = memsys->allocate( 0, size[ ipass ], 0, MEMORY_PERSISTENT );
		}
		time_start = time_current();
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			memsys->deallocate( ptr[ipass] );
			res.ops++;
		}
		time_end = time_current();
		res.elapsed += time_diff( time_start, time_end );
	}
	return res;
}


static benchmark_result_t _run_small_random_mixed_loop( memory_system_t* memsys, void** ptr, uint32_t* size )
{
	int iloop, ipass;
	tick_t time_start, time_end;
	benchmark_result_t res = {0};

	for( iloop = 0; iloop < 512; ++iloop )
	{
		time_start = time_current();
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			ptr[ipass] = memsys->allocate( 0, size[ ipass ], 0, MEMORY_PERSISTENT );
			++res.ops;
			if( ( ipass % 3 ) == 1 )
			{
				ptr[ipass] = memsys->reallocate( ptr[ipass], size[ ( ipass * iloop + 1 ) % 8192 ], 0, size[ ipass ] );
				++res.ops;
			}
			else if( ( ipass % 3 ) == 2 )
			{
				memsys->deallocate( ptr[ipass] );
				ptr[ipass] = memsys->allocate( 0, size[ ( ipass * iloop + 2 ) % 8192 ], 0, MEMORY_PERSISTENT );
				res.ops += 2;
			}
		}
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			memsys->deallocate( ptr[ipass] );
			++res.ops;
		}
		time_end = time_current();
		res.elapsed += time_diff( time_start, time_end );
	}
	return res;
}


static void _collect_thread_results( object_t* thread, benchmark_arg_t* arg, unsigned int num_thread, benchmark_result_t* res_avg, benchmark_result_t* res_worst, benchmark_result_t* res_best )
{
	unsigned int i;
	bool wait;

	do
	{
		wait = false;
		for( i = 0; i < num_thread; ++i )
		{
			if( !thread_is_started( thread[i] ) )
				wait = true;
		}
		thread_sleep( 100 );
	} while( wait );

	do
	{
		wait = false;
		for( i = 0; i < num_thread; ++i )
		{
			if( thread_is_running( thread[i] ) )
				wait = true;
		}
		thread_sleep( 100 );
	} while( wait );

	thread_sleep( 100 );

	res_avg->elapsed = 0;
	res_avg->ops = 0;

	res_worst->elapsed = arg[0].res.elapsed;
	res_worst->ops = arg[0].res.ops;

	res_best->elapsed = arg[0].res.elapsed;
	res_best->ops = arg[0].res.ops;

	for( i = 0; i < num_thread; ++i )
	{
		res_avg->elapsed += arg[i].res.elapsed;
		res_avg->ops += arg[i].res.ops;

		if( arg[i].res.elapsed > res_worst->elapsed )
		{
			res_worst->elapsed = arg[i].res.elapsed;
			res_worst->ops = arg[i].res.ops;			
		}

		if( arg[i].res.elapsed < res_best->elapsed )
		{
			res_best->elapsed = arg[i].res.elapsed;
			res_best->ops = arg[i].res.ops;			
		}
	}
	res_avg->elapsed /= num_thread;
	res_avg->ops /= num_thread;
}


int main_initialize( void )
{
	int ret = 0;

	application_t app = {0};
	app.name = "Memory allocation benchmark";
	app.short_name = "benchmark_alloc";
	app.config_dir = "benchmark_alloc";
	app.flags = APPLICATION_UTILITY;

	log_enable_prefix( false );
	log_set_suppress( 0, ERRORLEVEL_INFO );
	log_set_suppress( HASH_MEMORY, ERRORLEVEL_INFO );
	log_set_suppress( HASH_BENCHMARK, ERRORLEVEL_DEBUG );

	if( ( ret = foundation_initialize( memory_system_malloc(), app ) ) < 0 )
		return ret;

	config_set_int( HASH_FOUNDATION, HASH_TEMPORARY_MEMORY, 64 * 1024 );

	return 0;
}


int main_run( void* main_arg )
{
	int iloop, ipass, iseq;
	unsigned int i;
	benchmark_result_t res, res_worst, res_best;
	object_t thread[64];
	benchmark_arg_t arg[64];
	unsigned int num_thread;
	bool wait;

	memory_system_t sys_malloc = memory_system_malloc();
	memory_system_t sys_memory = memory_system();

	sys_malloc.initialize();
	sys_memory.initialize();

	for( iloop = 0; iloop < 64; ++iloop )
	{
		ptr_malloc[iloop] = sys_malloc.allocate( 0, sizeof( void* ) * 8192, 0, MEMORY_PERSISTENT );
		ptr_memory[iloop] = sys_memory.allocate( 0, sizeof( void* ) * 8192, 0, MEMORY_PERSISTENT );
	}

	random_size = memory_allocate( sizeof( uint32_t ) * 8192, 0, MEMORY_PERSISTENT );
	for( iloop = 0; iloop < 8192; ++iloop )
		random_size[iloop] = random32_range( 0, 8192 );

	num_thread = system_hardware_threads() * 2;
	if( num_thread < 3 )
		num_thread = 3;
	if( num_thread > 64 )
		num_thread = 64;

	//Warmup phase
	for( iloop = 0; iloop < 64; ++iloop )
	{
		for( ipass = 0; ipass < 8192; ++ipass )
		{
			ptr_malloc[0][ipass] = sys_malloc.allocate( 0, ( ipass + iloop ), 0, MEMORY_PERSISTENT );
			ptr_memory[0][ipass] = sys_memory.allocate( 0, ( ipass + iloop ), 0, MEMORY_PERSISTENT );
		}

		for( ipass = 0; ipass < 8192; ++ipass )
		{
			sys_malloc.deallocate( ptr_malloc[0][ipass] );
			sys_memory.deallocate( ptr_memory[0][ipass] );
		}
	}

	log_infof( HASH_BENCHMARK, "Running on %u cores", system_hardware_threads() );

	log_info( HASH_BENCHMARK, "" );
	log_info( HASH_BENCHMARK, "Single threaded sequential small allocation");
	log_info( HASH_BENCHMARK, "===========================================");
	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_allocation_loop( &sys_malloc, ptr_malloc[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Malloc time: %.4" PRIREAL "s : %u allocs/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_allocation_loop( &sys_memory, ptr_memory[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Memory time: %.4" PRIREAL "s : %u allocs/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Single threaded random small allocation");
	log_info( HASH_BENCHMARK, "=======================================");
	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_random_allocation_loop( &sys_malloc, ptr_malloc[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Malloc time: %.4" PRIREAL "s : %u allocs/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_random_allocation_loop( &sys_memory, ptr_memory[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Memory time: %.4" PRIREAL "s : %u allocs/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Single threaded random reallocation");
	log_info( HASH_BENCHMARK, "===================================");
	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_random_reallocation_loop( &sys_malloc, ptr_malloc[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Malloc time: %.4" PRIREAL "s : %u reallocs/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_random_reallocation_loop( &sys_memory, ptr_memory[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Memory time: %.4" PRIREAL "s : %u reallocs/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Single threaded random deallocation");
	log_info( HASH_BENCHMARK, "===================================");
	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_random_deallocation_loop( &sys_malloc, ptr_malloc[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Malloc time: %.4" PRIREAL "s : %u deallocs/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_random_deallocation_loop( &sys_memory, ptr_memory[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Memory time: %.4" PRIREAL "s : %u deallocs/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Single threaded mixed allocation/reallocation/deallocation");
	log_info( HASH_BENCHMARK, "==========================================================");
	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_random_mixed_loop( &sys_malloc, ptr_malloc[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Malloc time: %.4" PRIREAL "s : %u ops/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	res.elapsed = 0;
	res.ops= 0;
	for( iseq = 0; iseq < 16; ++iseq )
	{
		benchmark_result_t current = _run_small_random_mixed_loop( &sys_memory, ptr_memory[0], random_size );
		res.elapsed += current.elapsed;
		res.ops += current.ops;
	}
	log_infof( HASH_BENCHMARK, "Memory time: %.4" PRIREAL "s : %u ops/s", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Multi threaded sequential small allocation");
	log_info( HASH_BENCHMARK, "==========================================");

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_allocation_loop;
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Malloc avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_allocation_loop;
		arg[i].memsys = &sys_memory;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Memory avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Multi threaded random small allocation");
	log_info( HASH_BENCHMARK, "======================================");

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_random_allocation_loop;
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Malloc avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_random_allocation_loop;
		arg[i].memsys = &sys_memory;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Memory avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Multi threaded random reallocation");
	log_info( HASH_BENCHMARK, "==================================");

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_random_reallocation_loop;
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Malloc avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_random_reallocation_loop;
		arg[i].memsys = &sys_memory;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Memory avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Multi threaded random deallocation");
	log_info( HASH_BENCHMARK, "==================================");

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_random_deallocation_loop;
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Malloc avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_random_deallocation_loop;
		arg[i].memsys = &sys_memory;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Memory avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	log_info( HASH_BENCHMARK, "");
	log_info( HASH_BENCHMARK, "Multi threaded random mixed allocation/reallocation/deallocation");
	log_info( HASH_BENCHMARK, "================================================================");

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_random_mixed_loop;
		arg[i].memsys = &sys_malloc;
		arg[i].ptr = ptr_malloc[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Malloc avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	for( i = 0; i < num_thread; ++i )
	{
		memset( arg + i, 0, sizeof( benchmark_arg_t ) );

		arg[i].function = _run_small_random_mixed_loop;
		arg[i].memsys = &sys_memory;
		arg[i].ptr = ptr_memory[i];
		arg[i].size = random_size;

		thread[i] = thread_create( benchmark_thread, "allocator", THREAD_PRIORITY_NORMAL, 0 );
	}

	for( i = 0; i < num_thread; ++i )
		thread_start( thread[i], arg + i );
	_collect_thread_results( thread, arg, num_thread, &res, &res_worst, &res_best );
	for( i = 0; i < num_thread; ++i )
		thread_destroy( thread[i] );
	log_infof( HASH_BENCHMARK, "Memory avg time: %.4" PRIREAL "s : %u ops/s (best %.4" PRIREAL "s, worst %.4" PRIREAL "s)", time_ticks_to_seconds( res.elapsed ), (unsigned int)((real)res.ops / time_ticks_to_seconds( res.elapsed ) ), time_ticks_to_seconds( res_best.elapsed ), time_ticks_to_seconds( res_worst.elapsed ) );

	for( iloop = 0; iloop < 64; ++iloop )
	{
		sys_malloc.deallocate( ptr_malloc[iloop] );
		sys_memory.deallocate( ptr_memory[iloop] );
	}

	memory_deallocate( random_size );

	sys_malloc.shutdown();
	sys_memory.shutdown();

	return 0;
}


void main_shutdown( void )
{
	foundation_shutdown();
}
