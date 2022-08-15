#pragma once

#include "pvp.h"

err_t rom_init(const char *fdt_path);
err_t rom_call(void);
err_t rom_fault(gea_t gea, gra_t *gra);
