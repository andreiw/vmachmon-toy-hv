#pragma once

#define ERR_LIST                                                      \
  ERR_DEF(ERR_NONE, "no error")                                       \
  ERR_DEF(ERR_ASSERT, "assertion failure")                            \
  ERR_DEF(ERR_NOT_READY, "not ready")                                 \
  ERR_DEF(ERR_BAD_ACCESS, "bad access to guest memory")               \
  ERR_DEF(ERR_OUT_OF_BOUNDS, "bound check")                           \
  ERR_DEF(ERR_IO_ERROR, "io error")                                   \
  ERR_DEF(ERR_UNSUPPORTED, "not supported")                           \
  ERR_DEF(ERR_NO_MEM, "no memory")                                    \
  ERR_DEF(ERR_NOT_FOUND, "not found")                                 \
  ERR_DEF(ERR_MACH, "Mach error")                                     \
  ERR_DEF(ERR_SHUTDOWN, "Requested shutdown")                         \
  ERR_DEF(ERR_CONTINUE, "Continue VM execution")                      \
  ERR_DEF(ERR_PAUSE, "Pause VM execution")                            \
  ERR_DEF(ERR_POSIX, "POSIX error")                                   \
  ERR_DEF(ERR_NOT_ROM_CALL, "Not a ROM call")                         \
  ERR_DEF(ERR_INVALID, "invalid error, likely a bug") /* last */

#define ERR_DEF(e, s) e,
typedef enum {
  ERR_LIST
} err_t;
#undef ERR_DEF

const char * err_to_string(err_t code);
