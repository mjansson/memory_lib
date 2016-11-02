/* log.h  -  Memory library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file log.h
    Log output */

#include <foundation/platform.h>
#include <foundation/hashstrings.h>

#include <memory/types.h>

#if BUILD_ENABLE_MEMORY_SPAM_LOG
#  define log_memory_spam(...) log_debug(HASH_MEMORY, __VA_ARGS__)
#  define log_memory_spamf(...) log_debugf(HASH_MEMORY, __VA_ARGS__)
#else
#  define log_memory_spam(...) do { FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0)
#  define log_memory_spamf(...) do { FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0)
#endif

#if BUILD_ENABLE_MEMORY_DEBUG_LOG
#  define log_memory_debug(...) log_debug(HASH_MEMORY, __VA_ARGS__)
#  define log_memory_debugf(...) log_debugf(HASH_MEMORY, __VA_ARGS__)
#else
#  define log_memory_debug(...) do { FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0)
#  define log_memory_debugf(...) do { FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0)
#endif

#if BUILD_ENABLE_MEMORY_LOG
#  define log_memory_info(...) log_info(HASH_MEMORY, __VA_ARGS__)
#  define log_memory_infof(...) log_infof(HASH_MEMORY, __VA_ARGS__)
#  define log_memory_warn(warn, ...) log_warn(HASH_MEMORY, warn, __VA_ARGS__)
#  define log_memory_warnf(warn, ...) log_warnf(HASH_MEMORY, warn, __VA_ARGS__)
#  define log_memory_error(err, ...) log_error( HASH_MEMORY, err, __VA_ARGS__ )
#  define log_memory_errorf(err, ...) log_errorf( HASH_MEMORY, err, __VA_ARGS__ )
#  define log_memory_panic(err, ...) log_panic( HASH_MEMORY, err, __VA_ARGS__ )
#  define log_memory_panicf(err, ...) log_panicf( HASH_MEMORY, err, __VA_ARGS__ )
#else
#  define log_memory_info(...) do { FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0)
#  define log_memory_infof(...) do { FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0)
#  define log_memory_warn(warn, ...) do { FOUNDATION_UNUSED(warn); FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0)
#  define log_memory_warnf(warn, ...) do { FOUNDATION_UNUSED(warn); FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0)
#  define log_memory_error(err, ...) do { error_report(ERRORLEVEL_ERROR, err); FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0) /*lint -restore */
#  define log_memory_errorf(err, ...) do { error_report(ERRORLEVEL_ERROR, err); FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0) /*lint -restore */
#  define log_memory_panic(err, ...) do { error_report(ERRORLEVEL_PANIC, err); FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0) /*lint -restore */
#  define log_memory_panicf(err, ...) do { error_report(ERRORLEVEL_PANIC, err); FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); } while(0) /*lint -restore */
#endif
