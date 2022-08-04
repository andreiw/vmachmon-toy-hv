#pragma once

#define ERR_LIST                                                      \
  ERR_DEF(ERR_NONE, "no error")                                       \
  ERR_DEF(ERR_ASSERT, "assertion failure")                            \
  ERR_DEF(ERR_NOT_READY, "not ready")                                 \
  ERR_DEF(ERR_BAD_ACCESS, "bad access to guest memory")               \
  ERR_DEF(ERR_UNSUPPORTED, "not supported")                           \
  ERR_DEF(ERR_NO_MEM, "no memory")                                    \
  ERR_DEF(ERR_NOT_FOUND, "not found")                                 \
  ERR_DEF(ERR_MACH, "Mach error")                                     \
  ERR_DEF(ERR_POSIX, "POSIX error")                                   \
  ERR_DEF(ERR_INVALID, "invalid error, likely a bug") /* last */

#define ERR_DEF(e, s) e,
typedef enum {
  ERR_LIST
} err_t;
#undef ERR_DEF

typedef uint32_t length_t;
typedef uint32_t offset_t;
typedef uint32_t count_t;
typedef uint32_t ha_t;
typedef uint32_t gra_t;
typedef uint32_t gea_t;
