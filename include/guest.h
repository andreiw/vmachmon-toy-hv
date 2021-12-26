#pragma once

#include <pvp.h>
#include <vmm.h>

typedef struct guest_t {
  uint32_t sdr1;
  uint32_t pvr;
  uint32_t srr0;
  uint32_t srr1;
  uint32_t ibat[8];
  uint32_t dbat[8];
  uint32_t sr[16];
  uint32_t hid0;
  size_t ram_size;
  vmm_state_page_t *vmm;
  vmm_thread_index_t vmm_index;
  vmm_regs32_t *regs;
} guest_t;

extern guest_t *guest;

err_t guest_init(bool little, size_t ram_size);
bool guest_is_little(void);
