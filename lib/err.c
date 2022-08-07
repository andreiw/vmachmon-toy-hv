#include "pvp.h"

const char *
err_to_string(err_t code)
{
#define ERR_DEF(x, z) case x: { return z; } 

  if (code > ERR_INVALID) {
    code = ERR_INVALID;
  }
  switch(code) {
  default:
    ERR_LIST
  }
#undef ERR_DEF
}
