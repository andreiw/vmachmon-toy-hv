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
  uint32_t sr[16];
  uint32_t hid0;
  uint32_t msr;
  /*
   * Bits the VMM may force on transparent of the guest state.
   * This could be tracing (single-stepping, branch trace) or
   * performance counters.
   */
  uint32_t mon_msr;
  vmm_state_page_t *vmm;
  vmm_state_page_t *vmm_mmu_on;
  vmm_state_page_t *vmm_mmu_off;
  vmm_regs32_t *regs;
} guest_t;

extern guest_t *guest;

err_t guest_init(bool little, length_t ram_size);
void guest_bye(void);
bool guest_is_little(void);
err_t guest_map(ha_t host_address, gea_t ea);
err_t guest_backmap(gea_t ea, gra_t *gra);
err_t guest_from(void *dest, gea_t src, length_t bytes,
                 length_t access_size);
length_t guest_from_ex(void *dest, gea_t src, length_t bytes,
                       length_t access_size, bool nul_term);
err_t guest_to(gra_t dest, const void *src, length_t bytes,
               length_t access_size);
bool guest_toggle_ss(void);
err_t guest_emulate(void);

#define guest_to_x(dest, src) guest_to(dest, src, sizeof(*(src)), sizeof(*(src)))
#define guest_from_x(dest, src) guest_from(dest, src, sizeof(*(dest)), sizeof(*(dest)))
