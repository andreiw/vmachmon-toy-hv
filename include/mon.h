#pragma once

#include "pvp.h"

err_t mon_init(void);
void mon_bye(void);
err_t mon_activate(void);
err_t mon_check(void);
err_t mon_trace(void);
int mon_fprintf(void *unused, const char *fmt, ...);

#define mon_printf(fmt, ...) mon_fprintf(NULL,  fmt, ## __VA_ARGS__)
