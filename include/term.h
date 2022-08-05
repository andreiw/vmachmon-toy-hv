#pragma once

#include "pvp.h"

err_t term_init(const char *con_path);
void term_out(const char *buf, length_t len);
