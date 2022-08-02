#pragma once

#include "pvp.h"
#include "vmm.h"

typedef enum {
  MMU_PSEUDO_ON,
  MMU_OFF,
  MMU_ON,
} mmu_state_t;

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
  mmu_state_t mmu_state;
} guest_t;

extern guest_t *guest;

err_t guest_init(bool little, size_t ram_size);
bool guest_is_little(void);
bool guest_mmu_allow_ra(void);
err_t guest_map(ha_t host_addres, gea_t ea);
err_t guest_backmap(gea_t ea, gra_t *gra);
