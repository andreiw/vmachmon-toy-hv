#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#ifndef LOG_PFX
#define LOG_PFX PVP
#endif /* LOG_PFX */

#define LOG_FATAL       0
#define LOG_ERROR       1
#define LOG_FDT_ERROR   2
#define LOG_MACH_ERROR  3
#define LOG_VMM_ERROR   4
#define LOG_POSIX_ERROR 5
#define LOG_WARN        6
#define LOG_NORMAL      7
#define LOG_VERBOSE     8
#define LOG_DEBUG       9
#define LOG_MAX         10

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

#define LOG_FL_S "%s(%u): "

#define ON_ERROR(msg, error, out) \
  if (error != ERR_NONE) { ERROR(error, LOG_FL_S msg, __FILE__, __LINE__); goto out; }

#define ON_MACH_ERROR(msg, error, out) \
  if (error != KERN_SUCCESS) { MACH_ERROR(error, LOG_FL_S msg, __FILE__, __LINE__); goto out; }

#define ON_POSIX_ERROR(msg, error, out) \
  if (error < 0) { POSIX_ERROR(errno, LOG_FL_S msg, __FILE__, __LINE__); goto out; }

#define ON_VMM_ERROR(msg, error, out) \
  if (error != kVmmReturnNull) { VMM_ERROR(error, msg); goto out; }

#define BUG_ON(x, fmt, ...) do {                                        \
    if ((x)) {                                                          \
      FATAL(ERR_ASSERT, LOG_FL_S "%s - " fmt, __FILE__, __LINE__, SIFY(x), ## __VA_ARGS__); \
      abort();                                                          \
    }                                                                   \
  } while(0);
