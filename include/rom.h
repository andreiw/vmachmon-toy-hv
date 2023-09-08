#pragma once

#include "pvp.h"
#include "guest.h"

err_t rom_init(const char *fdt_path);
err_t rom_call(void);
err_t rom_fault(gea_t gea, gra_t *gra,
                guest_fault_t flags);
void rom_mon_dump(void);
