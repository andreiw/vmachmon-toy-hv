#pragma once
#include "pvp.h"

err_t term_init(void);
void term_out(const char *buf, length_t len);
length_t term_in(char *buf, length_t expected);
void term_bye(void);
