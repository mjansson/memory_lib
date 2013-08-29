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

#include <memory/types.h>


#if BUILD_ENABLE_MEMORY_SPAM_LOG
#  define               log_memory_spamf( msg, ... ) log_debugf( msg, __VA_ARGS__ )
#else
#  define               log_memory_spamf( msg, ... ) /*lint -save -e717 */ do { (void)sizeof( msg ); } while(0) /*lint -restore */
#endif

#if BUILD_ENABLE_MEMORY_DEBUG_LOG
#  define               log_memory_debugf( msg, ... ) log_debugf( msg, __VA_ARGS__ )
#else
#  define               log_memory_debugf( msg, ... ) /*lint -save -e717 */ do { (void)sizeof( msg ); } while(0) /*lint -restore */
#endif

#if BUILD_ENABLE_MEMORY_LOG
#  define               log_memory_infof( format, ... ) log_infof( format, __VA_ARGS__ )
#  define               log_memory_warnf( warn, format, ... ) log_warnf( warn, format, __VA_ARGS__ )
#  define               log_memory_errorf( err, format, ... ) log_errorf( err, format, __VA_ARGS__ )
#  define               log_memory_panicf( err, format, ... ) log_panicf( err, format, __VA_ARGS__ )
#else
#  define               log_memory_infof( msg, ... ) /*lint -save -e717 */ do { (void)sizeof( msg ); } while(0) /*lint -restore */
#  define               log_memory_warnf( warn, msg, ... ) /*lint -save -e717 */ do { (void)sizeof( warn ); (void)sizeof( msg ); } while(0) /*lint -restore */
#  define               log_memory_errorf( err, msg, ... ) /*lint -save -e717 */ do { error_report( ERRORLEVEL_ERROR, err ); (void)sizeof( msg ); } while(0) /*lint -restore */
#  define               log_memory_panicf( err, msg, ... ) /*lint -save -e717 */ do { error_report( ERRORLEVEL_PANIC, err ); (void)sizeof( msg ); } while(0) /*lint -restore */
#endif

#define log_memory_spam( msg )  log_memory_spamf( "%s", msg )
#define log_memory_debug( msg ) log_memory_debugf( "%s", msg )
#define log_memory_info( msg )  log_memory_infof( "%s", msg )
#define log_memory_warn( msg )  log_memory_warnf( "%s", msg )
#define log_memory_error( msg ) log_memory_errorf( "%s", msg )
#define log_memory_panic( msg ) log_memory_panicf( "%s", msg )
