#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

#ifndef LOG_PFX
#define LOG_PFX VMM
#endif /* LOG_PFX */

#define LOG_FATAL       0
#define LOG_ERROR       1
#define LOG_MACH_ERROR  2
#define LOG_VMM_ERROR   3
#define LOG_POSIX_ERROR 4
#define LOG_WARN        5
#define LOG_NORMAL      6
#define LOG_VERBOSE     7
#define LOG_DEBUG       8
#define LOG_MAX         9

#ifndef LOG_LVL
#define LOG_LVL LOG_DEBUG
#endif /* LOG_LVL */

void _log(unsigned level, unsigned log_lvl, unsigned error_extra,
          char *pfx, char *fmt, ...);
#define _LOG(level, error_extra, fmt, ...) \
  _log(level, LOG_LVL, error_extra, SIFY(LOG_PFX), fmt, ## __VA_ARGS__)

#define FATAL(error, fmt, ...)       _LOG(LOG_FATAL, error, fmt, ## __VA_ARGS__)
#define ERROR(error, fmt, ...)       _LOG(LOG_ERROR, error, fmt, ## __VA_ARGS__)
#define MACH_ERROR(error, fmt, ...)  _LOG(LOG_MACH_ERROR, error, fmt, ## __VA_ARGS__)
#define POSIX_ERROR(error, fmt, ...) _LOG(LOG_POSIX_ERROR, error, fmt, ## __VA_ARGS__)
#define VMM_ERROR(error, fmt, ...)   _LOG(LOG_VMM_ERROR, error, fmt, ## __VA_ARGS__)
#define WARN(fmt, ...)               _LOG(LOG_WARN, 0, fmt, ## __VA_ARGS__)
#define LOG(fmt, ...)                _LOG(LOG_NORMAL, 0, fmt, ## __VA_ARGS__)

#ifdef OPTIMIZED
#define VERBOSE(fmt, ...) _LOG(LOG_VERBOSE, 0, fmt, ## __VA_ARGS__)
#define DEBUG(fmt, ...)
#else /*  OPTIMIZED */
#define VERBOSE(fmt, ...) _LOG(LOG_VERBOSE, 0, fmt, ## __VA_ARGS__)
#define DEBUG(fmt, ...)   _LOG(LOG_DEBUG, 0, fmt, ## __VA_ARGS__)
#endif /* Not OPTIMIZED */

