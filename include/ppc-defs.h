#pragma once

#define MSR_LE (1UL << (0))

// PowerPC instruction template: add immediate, D-form
typedef struct I_addi_d_form {
    u_int32_t OP: 6;  // major opcode
    u_int32_t RT: 5;  // target register
    u_int32_t RA: 5;  // register operand
    u_int32_t SI: 16; // immediate operand
} I_addi_d_form;

// PowerPC instruction template: unconditional branch, I-form
typedef struct branch_i_form {
    u_int32_t OP: 6;  // major opcode
    u_int32_t LI: 24; // branch target (immediate)
    u_int32_t AA: 1;  // absolute or relative
    u_int32_t LK: 1;  // link or not
} I_branch_i_form;

// PowerPC instruction template: add, XO-form
typedef struct I_add_xo_form {
    u_int32_t OP: 6;  // major opcode
    u_int32_t RT: 5;  // target register
    u_int32_t RA: 5;  // register operand A
    u_int32_t RB: 5;  // register operand B
    u_int32_t OE: 1;  // alter SO, OV?
    u_int32_t XO: 9;  // extended opcode
    u_int32_t Rc: 1;  // alter CR0?
} I_add_xo_form;
