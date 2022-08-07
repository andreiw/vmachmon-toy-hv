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
  vmm_state_page_t *vmm;
  vmm_thread_index_t vmm_index;
  vmm_regs32_t *regs;
  mmu_state_t mmu_state;
  vmm_return_code_t vmm_ret;
} guest_t;

extern guest_t *guest;

err_t guest_init(bool little, length_t ram_size);
bool guest_is_little(void);
bool guest_mmu_allow_ra(void);
err_t guest_map(ha_t host_address, gea_t ea);
err_t guest_backmap(gea_t ea, gra_t *gra);
err_t guest_from(void *dest, gea_t src, length_t bytes,
                 length_t access_size);
length_t guest_from_ex(void *dest, gea_t src, length_t bytes,
                       length_t access_size, bool nul_term);
err_t guest_to(gra_t dest, const void *src, length_t bytes,
               length_t access_size);
void guest_dump();
bool guest_toggle_ss(void);

#define guest_to_x(dest, src) guest_to(dest, src, sizeof(*(src)), sizeof(*(src)))
#define guest_from_x(dest, src) guest_from(dest, src, sizeof(*(dest)), sizeof(*(dest)))
