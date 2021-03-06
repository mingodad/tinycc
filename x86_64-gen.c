/*
 *  x86-64 code generator for TCC
 *
 *  Copyright (c) 2008 Shinichiro Hamaji
 *
 *  Based on i386-gen.c by Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef TARGET_DEFS_ONLY

/* number of available registers */
#define NB_REGS         25
#define NB_ASM_REGS     8

/* a register can belong to several classes. The classes must be
   sorted from more general to more precise (see gv2() code which does
   assumptions on it). */
#define RC_INT     0x0001 /* generic integer register */
#define RC_FLOAT   0x0002 /* generic float register */
#define RC_RAX     0x0004
#define RC_RCX     0x0008
#define RC_RDX     0x0010
#define RC_ST0     0x0080 /* only for long double */
#define RC_R8      0x0100
#define RC_R9      0x0200
#define RC_R10     0x0400
#define RC_R11     0x0800
#define RC_XMM0    0x1000
#define RC_XMM1    0x2000
#define RC_XMM2    0x4000
#define RC_XMM3    0x8000
#define RC_XMM4    0x10000
#define RC_XMM5    0x20000
#define RC_XMM6    0x40000
#define RC_XMM7    0x80000
#define RC_IRET    RC_RAX /* function return: integer register */
#define RC_LRET    RC_RDX /* function return: second integer register */
#define RC_FRET    RC_XMM0 /* function return: float register */
#define RC_QRET    RC_XMM1 /* function return: second float register */

/* pretty names for the registers */
enum {
    TREG_RAX = 0,
    TREG_RCX = 1,
    TREG_RDX = 2,
    TREG_RSP = 4,
    TREG_RSI = 6,
    TREG_RDI = 7,

    TREG_R8  = 8,
    TREG_R9  = 9,
    TREG_R10 = 10,
    TREG_R11 = 11,

    TREG_XMM0 = 16,
    TREG_XMM1 = 17,
    TREG_XMM2 = 18,
    TREG_XMM3 = 19,
    TREG_XMM4 = 20,
    TREG_XMM5 = 21,
    TREG_XMM6 = 22,
    TREG_XMM7 = 23,

    TREG_ST0 = 24,

    TREG_MEM = 0x20,
};

#define REX_BASE(reg) (((reg) >> 3) & 1)
#define REG_VALUE(reg) ((reg) & 7)

/* return registers for function */
#define REG_IRET TREG_RAX /* single word int return register */
#define REG_LRET TREG_RDX /* second word return register (for long long) */
#define REG_FRET TREG_XMM0 /* float return register */
#define REG_QRET TREG_XMM1 /* second float return register */

/* defined if function parameters must be evaluated in reverse order */
#define INVERT_FUNC_PARAMS

/* pointer size, in bytes */
#define PTR_SIZE 8

/* long double size and alignment, in bytes */
#define LDOUBLE_SIZE  16
#define LDOUBLE_ALIGN 16
/* maximum alignment (for aligned attribute support) */
#define MAX_ALIGN     16

/******************************************************/
/* ELF defines */

#define EM_TCC_TARGET EM_X86_64

/* relocation type for 32 bit data relocation */
#define R_DATA_32   R_X86_64_32
#define R_DATA_PTR  R_X86_64_64
#define R_JMP_SLOT  R_X86_64_JUMP_SLOT
#define R_COPY      R_X86_64_COPY

#define ELF_START_ADDR 0x400000
#define ELF_PAGE_SIZE  0x200000

/******************************************************/
#else /* ! TARGET_DEFS_ONLY */
/******************************************************/
#include "tcc.h"
#include <assert.h>

ST_DATA const int reg_classes[NB_REGS] = {
    /* eax */ RC_INT | RC_RAX,
    /* ecx */ RC_INT | RC_RCX,
    /* edx */ RC_INT | RC_RDX,
    0,
    0,
    0,
    0,
    0,
    RC_R8,
    RC_R9,
    RC_R10,
    RC_R11,
    0,
    0,
    0,
    0,
    /* xmm0 */ RC_FLOAT | RC_XMM0,
    /* xmm1 */ RC_FLOAT | RC_XMM1,
    /* xmm2 */ RC_FLOAT | RC_XMM2,
    /* xmm3 */ RC_FLOAT | RC_XMM3,
    /* xmm4 */ RC_FLOAT | RC_XMM4,
    /* xmm5 */ RC_FLOAT | RC_XMM5,
    /* xmm6 an xmm7 are included so gv() can be used on them,
       but they are not tagged with RC_FLOAT because they are
       callee saved on Windows */
    RC_XMM6,
    RC_XMM7,
    /* st0 */ RC_ST0
};

//static unsigned long func_sub_sp_offset;
//static int func_ret_sub;

/* XXX: make it faster ? */
void g(TCCState* tcc_state, int c)
{
    int ind1;
    ind1 = tcc_state->tccgen_ind + 1;
    if (ind1 > tcc_state->tccgen_cur_text_section->data_allocated)
        section_realloc(tcc_state, tcc_state->tccgen_cur_text_section, ind1);
    tcc_state->tccgen_cur_text_section->data[tcc_state->tccgen_ind] = c;
    tcc_state->tccgen_ind = ind1;
}

void o(TCCState* tcc_state, unsigned int c)
{
    while (c) {
        g(tcc_state, c);
        c = c >> 8;
    }
}

void gen_le16(TCCState* tcc_state, int v)
{
    g(tcc_state, v);
    g(tcc_state, v >> 8);
}

void gen_le32(TCCState* tcc_state, int c)
{
    g(tcc_state, c);
    g(tcc_state, c >> 8);
    g(tcc_state, c >> 16);
    g(tcc_state, c >> 24);
}

void gen_le64(TCCState* tcc_state, int64_t c)
{
    g(tcc_state, c);
    g(tcc_state, c >> 8);
    g(tcc_state, c >> 16);
    g(tcc_state, c >> 24);
    g(tcc_state, c >> 32);
    g(tcc_state, c >> 40);
    g(tcc_state, c >> 48);
    g(tcc_state, c >> 56);
}

void orex(TCCState* tcc_state, int ll, int r, int r2, int b)
{
    if ((r & VT_VALMASK) >= VT_CONST)
        r = 0;
    if ((r2 & VT_VALMASK) >= VT_CONST)
        r2 = 0;
    if (ll || REX_BASE(r) || REX_BASE(r2))
        o(tcc_state, 0x40 | REX_BASE(r) | (REX_BASE(r2) << 2) | (ll << 3));
    o(tcc_state, b);
}

/* output a symbol and patch all calls to it */
void gsym_addr(TCCState *tcc_state, int t, int a)
{
    int n, *ptr;
    while (t) {
        ptr = (int *)(tcc_state->tccgen_cur_text_section->data + t);
        n = *ptr; /* next value */
        *ptr = a - t - 4;
        t = n;
    }
}

void gsym(TCCState* tcc_state, int t)
{
    gsym_addr(tcc_state, t, tcc_state->tccgen_ind);
}

/* psym is used to put an instruction with a data field which is a
   reference to a symbol. It is in fact the same as oad ! */
#define psym oad

static int is64_type(int t)
{
    return ((t & VT_BTYPE) == VT_PTR ||
            (t & VT_BTYPE) == VT_FUNC ||
            (t & VT_BTYPE) == VT_LLONG);
}

/* instruction + 4 bytes data. Return the address of the data */
ST_FUNC int oad(TCCState* tcc_state, int c, int s)
{
    int ind1;

    o(tcc_state, c);
    ind1 = tcc_state->tccgen_ind + 4;
    if (ind1 > tcc_state->tccgen_cur_text_section->data_allocated)
        section_realloc(tcc_state, tcc_state->tccgen_cur_text_section, ind1);
    *(int *)(tcc_state->tccgen_cur_text_section->data + tcc_state->tccgen_ind) = s;
    s = tcc_state->tccgen_ind;
    tcc_state->tccgen_ind = ind1;
    return s;
}

ST_FUNC void gen_addr32(TCCState* tcc_state, int r, Sym *sym, int c)
{
    if (r & VT_SYM)
        greloc(tcc_state, tcc_state->tccgen_cur_text_section, sym, tcc_state->tccgen_ind, R_X86_64_32);
    gen_le32(tcc_state, c);
}

/* output constant with relocation if 'r & VT_SYM' is true */
ST_FUNC void gen_addr64(TCCState* tcc_state, int r, Sym *sym, int64_t c)
{
    if (r & VT_SYM)
        greloc(tcc_state, tcc_state->tccgen_cur_text_section, sym, tcc_state->tccgen_ind, R_X86_64_64);
    gen_le64(tcc_state, c);
}

/* output constant with relocation if 'r & VT_SYM' is true */
ST_FUNC void gen_addrpc32(TCCState* tcc_state, int r, Sym *sym, int c)
{
    if (r & VT_SYM)
        greloc(tcc_state, tcc_state->tccgen_cur_text_section, sym, tcc_state->tccgen_ind, R_X86_64_PC32);
    gen_le32(tcc_state, c-4);
}

/* output got address with relocation */
static void gen_gotpcrel(TCCState* tcc_state, int r, Sym *sym, int c)
{
#ifndef TCC_TARGET_PE
    Section *sr;
    ElfW(Rela) *rel;
    greloc(tcc_state, tcc_state->tccgen_cur_text_section, sym, tcc_state->tccgen_ind, R_X86_64_GOTPCREL);
    sr = tcc_state->tccgen_cur_text_section->reloc;
    rel = (ElfW(Rela) *)(sr->data + sr->data_offset - sizeof(ElfW(Rela)));
    rel->r_addend = -4;
#else
    printf("picpic: %s %x %x | %02x %02x %02x\n", get_tok_str(tcc_state, sym->v, NULL), c, r,
        tcc_state->tccgen_cur_text_section->data[tcc_state->tccgen_ind-3],
        tcc_state->tccgen_cur_text_section->data[tcc_state->tccgen_ind-2],
        tcc_state->tccgen_cur_text_section->data[tcc_state->tccgen_ind-1]
        );
    greloc(tcc_state, tcc_state->tccgen_cur_text_section, sym, tcc_state->tccgen_ind, R_X86_64_PC32);
#endif
    gen_le32(tcc_state, 0);
    if (c) {
        /* we use add c, %xxx for displacement */
        orex(tcc_state, 1, r, 0, 0x81);
        o(tcc_state, 0xc0 + REG_VALUE(r));
        gen_le32(tcc_state, c);
    }
}

static void gen_modrm_impl(TCCState* tcc_state, int op_reg, int r, Sym *sym, int c, int is_got)
{
    op_reg = REG_VALUE(op_reg) << 3;
    if ((r & VT_VALMASK) == VT_CONST) {
        /* constant memory reference */
        o(tcc_state, 0x05 | op_reg);
        if (is_got) {
            gen_gotpcrel(tcc_state, r, sym, c);
        } else {
            gen_addrpc32(tcc_state, r, sym, c);
        }
    } else if ((r & VT_VALMASK) == VT_LOCAL) {
        /* currently, we use only ebp as base */
        if (c == (char)c) {
            /* short reference */
            o(tcc_state, 0x45 | op_reg);
            g(tcc_state, c);
        } else {
            oad(tcc_state, 0x85 | op_reg, c);
        }
    } else if ((r & VT_VALMASK) >= TREG_MEM) {
        if (c) {
            g(tcc_state, 0x80 | op_reg | REG_VALUE(r));
            gen_le32(tcc_state, c);
        } else {
            g(tcc_state, 0x00 | op_reg | REG_VALUE(r));
        }
    } else {
        g(tcc_state, 0x00 | op_reg | REG_VALUE(r));
    }
}

/* generate a modrm reference. 'op_reg' contains the addtionnal 3
   opcode bits */
static void gen_modrm(TCCState* tcc_state, int op_reg, int r, Sym *sym, int c)
{
    gen_modrm_impl(tcc_state, op_reg, r, sym, c, 0);
}

/* generate a modrm reference. 'op_reg' contains the addtionnal 3
   opcode bits */
static void gen_modrm64(TCCState* tcc_state, int opcode, int op_reg, int r, Sym *sym, int c)
{
    int is_got;
    is_got = (op_reg & TREG_MEM) && !(sym && (sym->type.t & VT_STATIC));
    orex(tcc_state, 1, r, op_reg, opcode);
    gen_modrm_impl(tcc_state, op_reg, r, sym, c, is_got);
}


/* load 'r' from value 'sv' */
void load(TCCState* tcc_state, int r, SValue *sv)
{
    int v, t, ft, fc, fr;
    SValue v1;

#ifdef TCC_TARGET_PE
    SValue v2;
    sv = pe_getimport(tcc_state, sv, &v2);
#endif

    fr = sv->r;
    ft = sv->type.t & ~VT_DEFSIGN;
    fc = sv->c.ul;

#ifndef TCC_TARGET_PE
    /* we use indirect access via got */
    if ((fr & VT_VALMASK) == VT_CONST && (fr & VT_SYM) &&
        (fr & VT_LVAL) && !(sv->sym->type.t & VT_STATIC)) {
        /* use the result register as a temporal register */
        int tr = r | TREG_MEM;
        if (is_float(ft)) {
            /* we cannot use float registers as a temporal register */
            tr = get_reg(tcc_state, RC_INT) | TREG_MEM;
        }
        gen_modrm64(tcc_state, 0x8b, tr, fr, sv->sym, 0);

        /* load from the temporal register */
        fr = tr | VT_LVAL;
    }
#endif

    v = fr & VT_VALMASK;
    if (fr & VT_LVAL) {
        int b, ll;
        if (v == VT_LLOCAL) {
            v1.type.t = VT_PTR;
            v1.r = VT_LOCAL | VT_LVAL;
            v1.c.ul = fc;
            fr = r;
            if (!(reg_classes[fr] & RC_INT))
                fr = get_reg(tcc_state, RC_INT);
            load(tcc_state, fr, &v1);
        }
        ll = 0;
        if ((ft & VT_BTYPE) == VT_FLOAT) {
            b = 0x6e0f66;
            r = REG_VALUE(r); /* movd */
        } else if ((ft & VT_BTYPE) == VT_DOUBLE) {
            b = 0x7e0ff3; /* movq */
            r = REG_VALUE(r);
        } else if ((ft & VT_BTYPE) == VT_LDOUBLE) {
            b = 0xdb, r = 5; /* fldt */
        } else if ((ft & VT_TYPE) == VT_BYTE || (ft & VT_TYPE) == VT_BOOL) {
            b = 0xbe0f;   /* movsbl */
        } else if ((ft & VT_TYPE) == (VT_BYTE | VT_UNSIGNED)) {
            b = 0xb60f;   /* movzbl */
        } else if ((ft & VT_TYPE) == VT_SHORT) {
            b = 0xbf0f;   /* movswl */
        } else if ((ft & VT_TYPE) == (VT_SHORT | VT_UNSIGNED)) {
            b = 0xb70f;   /* movzwl */
        } else {
            assert(((ft & VT_BTYPE) == VT_INT) || ((ft & VT_BTYPE) == VT_LLONG)
                   || ((ft & VT_BTYPE) == VT_PTR) || ((ft & VT_BTYPE) == VT_ENUM)
                   || ((ft & VT_BTYPE) == VT_FUNC));
            ll = is64_type(ft);
            b = 0x8b;
        }
        if (ll) {
            gen_modrm64(tcc_state, b, r, fr, sv->sym, fc);
        } else {
            orex(tcc_state, ll, fr, r, b);
            gen_modrm(tcc_state, r, fr, sv->sym, fc);
        }
    } else {
        if (v == VT_CONST) {
            if (fr & VT_SYM) {
#ifdef TCC_TARGET_PE
                orex(tcc_state, 1, 0, r, 0x8d);
                o(tcc_state, 0x05 + REG_VALUE(r) * 8); /* lea xx(%rip), r */
                gen_addrpc32(tcc_state, fr, sv->sym, fc);
#else
                if (sv->sym->type.t & VT_STATIC) {
                    orex(tcc_state, 1, 0, r, 0x8d);
                    o(tcc_state, 0x05 + REG_VALUE(r) * 8); /* lea xx(%rip), r */
                    gen_addrpc32(tcc_state, fr, sv->sym, fc);
                } else {
                    orex(tcc_state, 1, 0, r, 0x8b);
                    o(tcc_state, 0x05 + REG_VALUE(r) * 8); /* mov xx(%rip), r */
                    gen_gotpcrel(tcc_state, r, sv->sym, fc);
                }
#endif
            } else if (is64_type(ft)) {
                orex(tcc_state, 1, r, 0, 0xb8 + REG_VALUE(r)); /* mov $xx, r */
                gen_le64(tcc_state, sv->c.ull);
            } else {
                orex(tcc_state, 0, r, 0, 0xb8 + REG_VALUE(r)); /* mov $xx, r */
                gen_le32(tcc_state, fc);
            }
        } else if (v == VT_LOCAL) {
            orex(tcc_state, 1, 0, r, 0x8d); /* lea xxx(%ebp), r */
            gen_modrm(tcc_state, r, VT_LOCAL, sv->sym, fc);
        } else if (v == VT_CMP) {
            orex(tcc_state, 0, r, 0, 0);
	    if ((fc & ~0x100) != TOK_NE)
              oad(tcc_state, 0xb8 + REG_VALUE(r), 0); /* mov $0, r */
	    else
              oad(tcc_state, 0xb8 + REG_VALUE(r), 1); /* mov $1, r */
	    if (fc & 0x100)
	      {
	        /* This was a float compare.  If the parity bit is
		   set the result was unordered, meaning false for everything
		   except TOK_NE, and true for TOK_NE.  */
		fc &= ~0x100;
		o(tcc_state, 0x037a + (REX_BASE(r) << 8));
	      }
            orex(tcc_state, 0, r, 0, 0x0f); /* setxx %br */
            o(tcc_state, fc);
            o(tcc_state, 0xc0 + REG_VALUE(r));
        } else if (v == VT_JMP || v == VT_JMPI) {
            t = v & 1;
            orex(tcc_state, 0, r, 0, 0);
            oad(tcc_state, 0xb8 + REG_VALUE(r), t); /* mov $1, r */
            o(tcc_state, 0x05eb + (REX_BASE(r) << 8)); /* jmp after */
            gsym(tcc_state, fc);
            orex(tcc_state, 0, r, 0, 0);
            oad(tcc_state, 0xb8 + REG_VALUE(r), t ^ 1); /* mov $0, r */
        } else if (v != r) {
            if ((r >= TREG_XMM0) && (r <= TREG_XMM7)) {
                if (v == TREG_ST0) {
                    /* gen_cvt_ftof(VT_DOUBLE); */
                    o(tcc_state, 0xf0245cdd); /* fstpl -0x10(%rsp) */
                    /* movsd -0x10(%rsp),%xmmN */
                    o(tcc_state, 0x100ff2);
                    o(tcc_state, 0x44 + REG_VALUE(r)*8); /* %xmmN */
                    o(tcc_state, 0xf024);
                } else {
                    assert((v >= TREG_XMM0) && (v <= TREG_XMM7));
                    if ((ft & VT_BTYPE) == VT_FLOAT) {
                        o(tcc_state, 0x100ff3);
                    } else {
                        assert((ft & VT_BTYPE) == VT_DOUBLE);
                        o(tcc_state, 0x100ff2);
                    }
                    o(tcc_state, 0xc0 + REG_VALUE(v) + REG_VALUE(r)*8);
                }
            } else if (r == TREG_ST0) {
                assert((v >= TREG_XMM0) && (v <= TREG_XMM7));
                /* gen_cvt_ftof(VT_LDOUBLE); */
                /* movsd %xmmN,-0x10(%rsp) */
                o(tcc_state, 0x110ff2);
                o(tcc_state, 0x44 + REG_VALUE(r)*8); /* %xmmN */
                o(tcc_state, 0xf024);
                o(tcc_state, 0xf02444dd); /* fldl -0x10(%rsp) */
            } else {
                orex(tcc_state, 1, r, v, 0x89);
                o(tcc_state, 0xc0 + REG_VALUE(r) + REG_VALUE(v) * 8); /* mov v, r */
            }
        }
    }
}

/* store register 'r' in lvalue 'v' */
void store(TCCState* tcc_state, int r, SValue *v)
{
    int fr, bt, ft, fc;
    int op64 = 0;
    /* store the REX prefix in this variable when PIC is enabled */
    int pic = 0;

#ifdef TCC_TARGET_PE
    SValue v2;
    v = pe_getimport(tcc_state, v, &v2);
#endif

    ft = v->type.t;
    fc = v->c.ul;
    fr = v->r & VT_VALMASK;
    bt = ft & VT_BTYPE;

#ifndef TCC_TARGET_PE
    /* we need to access the variable via got */
    if (fr == VT_CONST && (v->r & VT_SYM)) {
        /* mov xx(%rip), %r11 */
        o(tcc_state, 0x1d8b4c);
        gen_gotpcrel(tcc_state, TREG_R11, v->sym, v->c.ul);
        pic = is64_type(bt) ? 0x49 : 0x41;
    }
#endif

    /* XXX: incorrect if float reg to reg */
    if (bt == VT_FLOAT) {
        o(tcc_state, 0x66);
        o(tcc_state, pic);
        o(tcc_state, 0x7e0f); /* movd */
        r = REG_VALUE(r);
    } else if (bt == VT_DOUBLE) {
        o(tcc_state, 0x66);
        o(tcc_state, pic);
        o(tcc_state, 0xd60f); /* movq */
        r = REG_VALUE(r);
    } else if (bt == VT_LDOUBLE) {
        o(tcc_state, 0xc0d9); /* fld %st(0) */
        o(tcc_state, pic);
        o(tcc_state, 0xdb); /* fstpt */
        r = 7;
    } else {
        if (bt == VT_SHORT)
            o(tcc_state, 0x66);
        o(tcc_state, pic);
        if (bt == VT_BYTE || bt == VT_BOOL)
            orex(tcc_state, 0, 0, r, 0x88);
        else if (is64_type(bt))
            op64 = 0x89;
        else
            orex(tcc_state, 0, 0, r, 0x89);
    }
    if (pic) {
        /* xxx r, (%r11) where xxx is mov, movq, fld, or etc */
        if (op64)
            o(tcc_state, op64);
        o(tcc_state, 3 + (r << 3));
    } else if (op64) {
        if (fr == VT_CONST || fr == VT_LOCAL || (v->r & VT_LVAL)) {
            gen_modrm64(tcc_state, op64, r, v->r, v->sym, fc);
        } else if (fr != r) {
            /* XXX: don't we really come here? */
            abort();
            o(tcc_state, 0xc0 + fr + r * 8); /* mov r, fr */
        }
    } else {
        if (fr == VT_CONST || fr == VT_LOCAL || (v->r & VT_LVAL)) {
            gen_modrm(tcc_state, r, v->r, v->sym, fc);
        } else if (fr != r) {
            /* XXX: don't we really come here? */
            abort();
            o(tcc_state, 0xc0 + fr + r * 8); /* mov r, fr */
        }
    }
}

/* 'is_jmp' is '1' if it is a jump */
static void gcall_or_jmp(TCCState* tcc_state, int is_jmp)
{
    int r;
    if ((tcc_state->tccgen_vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
        /* constant case */
        if (tcc_state->tccgen_vtop->r & VT_SYM) {
            /* relocation case */
            greloc(tcc_state, tcc_state->tccgen_cur_text_section, tcc_state->tccgen_vtop->sym,
                   tcc_state->tccgen_ind + 1, R_X86_64_PLT32);
        } else {
            /* put an empty PC32 relocation */
            put_elf_reloc(tcc_state, tcc_state->tccgen_symtab_section, tcc_state->tccgen_cur_text_section,
                          tcc_state->tccgen_ind + 1, R_X86_64_PC32, 0);
        }
        oad(tcc_state, 0xe8 + is_jmp, tcc_state->tccgen_vtop->c.ul - 4); /* call/jmp im */
    } else {
        /* otherwise, indirect call */
        r = TREG_R11;
        load(tcc_state, r, tcc_state->tccgen_vtop);
        o(tcc_state, 0x41); /* REX */
        o(tcc_state, 0xff); /* call/jmp *r */
        o(tcc_state, 0xd0 + REG_VALUE(r) + (is_jmp << 4));
    }
}

#ifdef TCC_TARGET_PE

#define REGN 4
static const uint8_t arg_regs[REGN] = {
    TREG_RCX, TREG_RDX, TREG_R8, TREG_R9
};

/* Prepare arguments in R10 and R11 rather than RCX and RDX
   because gv() will not ever use these */
static int arg_prepare_reg(int idx) {
  if (idx == 0 || idx == 1)
      /* idx=0: r10, idx=1: r11 */
      return idx + 10;
  else
      return arg_regs[idx];
}

static int func_scratch;

/* Generate function call. The function address is pushed first, then
   all the parameters in call order. This functions pops all the
   parameters and the function address. */

void gen_offs_sp(TCCState* tcc_state, int b, int r, int d)
{
    orex(tcc_state, 1, 0, r & 0x100 ? 0 : r, b);
    if (d == (char)d) {
        o(tcc_state, 0x2444 | (REG_VALUE(r) << 3));
        g(tcc_state, d);
    } else {
        o(tcc_state, 0x2484 | (REG_VALUE(r) << 3));
        gen_le32(tcc_state, d);
    }
}

/* Return the number of registers needed to return the struct, or 0 if
   returning via struct pointer. */
ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret, int *ret_align)
{
    int size, align;
    *ret_align = 1; // Never have to re-align return values for x86-64
    size = type_size(vt, &align);
    ret->ref = NULL;
    if (size > 8) {
        return 0;
    } else if (size > 4) {
        ret->t = VT_LLONG;
        return 1;
    } else if (size > 2) {
        ret->t = VT_INT;
        return 1;
    } else if (size > 1) {
        ret->t = VT_SHORT;
        return 1;
    } else {
        ret->t = VT_BYTE;
        return 1;
    }
}

static int is_sse_float(int t) {
    int bt;
    bt = t & VT_BTYPE;
    return bt == VT_DOUBLE || bt == VT_FLOAT;
}

int gfunc_arg_size(CType *type) {
    int align;
    if (type->t & (VT_ARRAY|VT_BITFIELD))
        return 8;
    return type_size(type, &align);
}

void gfunc_call(TCCState *tcc_state, int nb_args)
{
    int size, r, args_size, i, d, bt, struct_size;
    int arg;

    args_size = (nb_args < REGN ? REGN : nb_args) * PTR_SIZE;
    arg = nb_args;

    /* for struct arguments, we need to call memcpy and the function
       call breaks register passing arguments we are preparing.
       So, we process arguments which will be passed by stack first. */
    struct_size = args_size;
    for(i = 0; i < nb_args; i++) {
        SValue *sv;
        
        --arg;
        sv = &tcc_state->tccgen_vtop[-i];
        bt = (sv->type.t & VT_BTYPE);
        size = gfunc_arg_size(&sv->type);

        if (size <= 8)
            continue; /* arguments smaller than 8 bytes passed in registers or on stack */

        if (bt == VT_STRUCT) {
            /* align to stack align size */
            size = (size + 15) & ~15;
            /* generate structure store */
            r = get_reg(tcc_state, RC_INT);
            gen_offs_sp(tcc_state, 0x8d, r, struct_size);
            struct_size += size;

            /* generate memcpy call */
            vset(tcc_state, &sv->type, r | VT_LVAL, 0);
            vpushv(tcc_state, sv);
            vstore(tcc_state);
            --tcc_state->tccgen_vtop;
        } else if (bt == VT_LDOUBLE) {
            gv(tcc_state, RC_ST0);
            gen_offs_sp(tcc_state, 0xdb, 0x107, struct_size);
            struct_size += 16;
        }
    }

    if (func_scratch < struct_size)
        func_scratch = struct_size;

    arg = nb_args;
    struct_size = args_size;

    for(i = 0; i < nb_args; i++) {
        --arg;
        bt = (tcc_state->tccgen_vtop->type.t & VT_BTYPE);

        size = gfunc_arg_size(&tcc_state->tccgen_vtop->type);
        if (size > 8) {
            /* align to stack align size */
            size = (size + 15) & ~15;
            if (arg >= REGN) {
                d = get_reg(tcc_state, RC_INT);
                gen_offs_sp(tcc_state, 0x8d, d, struct_size);
                gen_offs_sp(tcc_state, 0x89, d, arg*8);
            } else {
                d = arg_prepare_reg(arg);
                gen_offs_sp(tcc_state, 0x8d, d, struct_size);
            }
            struct_size += size;
        } else {
            if (is_sse_float(tcc_state->tccgen_vtop->type.t)) {
                gv(tcc_state, RC_XMM0); /* only use one float register */
                if (arg >= REGN) {
                    /* movq %xmm0, j*8(%rsp) */
                    gen_offs_sp(tcc_state, 0xd60f66, 0x100, arg*8);
                } else {
                    /* movaps %xmm0, %xmmN */
                    o(tcc_state, 0x280f);
                    o(tcc_state, 0xc0 + (arg << 3));
                    d = arg_prepare_reg(arg);
                    /* mov %xmm0, %rxx */
                    o(tcc_state, 0x66);
                    orex(tcc_state, 1, d, 0, 0x7e0f);
                    o(tcc_state, 0xc0 + REG_VALUE(d));
                }
            } else {
                if (bt == VT_STRUCT) {
                    tcc_state->tccgen_vtop->type.ref = NULL;
                    tcc_state->tccgen_vtop->type.t = size > 4 ? VT_LLONG : size > 2 ? VT_INT
                        : size > 1 ? VT_SHORT : VT_BYTE;
                }
                
                r = gv(tcc_state, RC_INT);
                if (arg >= REGN) {
                    gen_offs_sp(tcc_state, 0x89, r, arg*8);
                } else {
                    d = arg_prepare_reg(arg);
                    orex(tcc_state, 1, d, r, 0x89); /* mov */
                    o(tcc_state, 0xc0 + REG_VALUE(r) * 8 + REG_VALUE(d));
                }
            }
        }
        tcc_state->tccgen_vtop--;
    }
    save_regs(tcc_state, 0);
    
    /* Copy R10 and R11 into RCX and RDX, respectively */
    if (nb_args > 0) {
        o(tcc_state, 0xd1894c); /* mov %r10, %rcx */
        if (nb_args > 1) {
            o(tcc_state, 0xda894c); /* mov %r11, %rdx */
        }
    }
    
    gcall_or_jmp(tcc_state, 0);
    tcc_state->tccgen_vtop--;
}


#define FUNC_PROLOG_SIZE 11

/* generate function prolog of type 't' */
void gfunc_prolog(TCCState *tcc_state, CType *func_type)
{
    int addr, reg_param_index, bt, size;
    Sym *sym;
    CType *type;

    tcc_state->func_ret_sub = 0;
    func_scratch = 0;
    tcc_state->tccgen_loc = 0;

    addr = PTR_SIZE * 2;
    tcc_state->tccgen_ind += FUNC_PROLOG_SIZE;
    tcc_state->func_sub_sp_offset = tcc_state->tccgen_ind;
    reg_param_index = 0;

    sym = func_type->ref;

    /* if the function returns a structure, then add an
       implicit pointer parameter */
    tcc_state->tccgen_func_vt = sym->type;
    tcc_state->tccgen_func_var = (sym->c == FUNC_ELLIPSIS);
    size = gfunc_arg_size(&tcc_state->tccgen_func_vt);
    if (size > 8) {
        gen_modrm64(tcc_state, 0x89, arg_regs[reg_param_index], VT_LOCAL, NULL, addr);
        tcc_state->tccgen_func_vc = addr;
        reg_param_index++;
        addr += 8;
    }

    /* define parameters */
    while ((sym = sym->next) != NULL) {
        type = &sym->type;
        bt = type->t & VT_BTYPE;
        size = gfunc_arg_size(type);
        if (size > 8) {
            if (reg_param_index < REGN) {
                gen_modrm64(tcc_state, 0x89, arg_regs[reg_param_index], VT_LOCAL, NULL, addr);
            }
            sym_push(tcc_state, sym->v & ~SYM_FIELD, type, VT_LOCAL | VT_LVAL | VT_REF, addr);
        } else {
            if (reg_param_index < REGN) {
                /* save arguments passed by register */
                if ((bt == VT_FLOAT) || (bt == VT_DOUBLE)) {
                    o(tcc_state, 0xd60f66); /* movq */
                    gen_modrm(tcc_state, reg_param_index, VT_LOCAL, NULL, addr);
                } else {
                    gen_modrm64(tcc_state, 0x89, arg_regs[reg_param_index], VT_LOCAL, NULL, addr);
                }
            }
            sym_push(tcc_state, sym->v & ~SYM_FIELD, type, VT_LOCAL | VT_LVAL, addr);
        }
        addr += 8;
        reg_param_index++;
    }

    while (reg_param_index < REGN) {
        if (func_type->ref->c == FUNC_ELLIPSIS) {
            gen_modrm64(tcc_state, 0x89, arg_regs[reg_param_index], VT_LOCAL, NULL, addr);
            addr += 8;
        }
        reg_param_index++;
    }
}

/* generate function epilog */
void gfunc_epilog(TCCState *tcc_state)
{
    int v, saved_ind;

    o(tcc_state, 0xc9); /* leave */
    if (tcc_state->func_ret_sub == 0) {
        o(tcc_state, 0xc3); /* ret */
    } else {
        o(tcc_state, 0xc2); /* ret n */
        g(tcc_state, tcc_state->func_ret_sub);
        g(tcc_state, tcc_state->func_ret_sub >> 8);
    }

    saved_ind = tcc_state->tccgen_ind;
    tcc_state->tccgen_ind = tcc_state->func_sub_sp_offset - FUNC_PROLOG_SIZE;
    /* align local size to word & save local variables */
    v = (func_scratch + -tcc_state->tccgen_loc + 15) & -16;

    if (v >= 4096) {
        Sym *sym = external_global_sym(tcc_state, TOK___chkstk, &tcc_state->tccgen_func_old_type, 0);
        oad(tcc_state, 0xb8, v); /* mov stacksize, %eax */
        oad(tcc_state, 0xe8, -4); /* call __chkstk, (does the stackframe too) */
        greloc(tcc_state, tcc_state->tccgen_cur_text_section, sym, tcc_state->tccgen_ind-4, R_X86_64_PC32);
        o(tcc_state, 0x90); /* fill for FUNC_PROLOG_SIZE = 11 bytes */
    } else {
        o(tcc_state, 0xe5894855);  /* push %rbp, mov %rsp, %rbp */
        o(tcc_state, 0xec8148);  /* sub rsp, stacksize */
        gen_le32(tcc_state, v);
    }

    tcc_state->tccgen_cur_text_section->data_offset = saved_ind;
    pe_add_unwind_data(tcc_state, tcc_state->tccgen_ind, saved_ind, v);
    tcc_state->tccgen_ind = tcc_state->tccgen_cur_text_section->data_offset;
}

#else

static void gadd_sp(TCCState* tcc_state, int val)
{
    if (val == (char)val) {
        o(tcc_state, 0xc48348);
        g(tcc_state, val);
    } else {
        oad(tcc_state, 0xc48148, val); /* add $xxx, %rsp */
    }
}

typedef enum X86_64_Mode {
  x86_64_mode_none,
  x86_64_mode_memory,
  x86_64_mode_integer,
  x86_64_mode_sse,
  x86_64_mode_x87
} X86_64_Mode;

static X86_64_Mode classify_x86_64_merge(X86_64_Mode a, X86_64_Mode b)
{
    if (a == b)
        return a;
    else if (a == x86_64_mode_none)
        return b;
    else if (b == x86_64_mode_none)
        return a;
    else if ((a == x86_64_mode_memory) || (b == x86_64_mode_memory))
        return x86_64_mode_memory;
    else if ((a == x86_64_mode_integer) || (b == x86_64_mode_integer))
        return x86_64_mode_integer;
    else if ((a == x86_64_mode_x87) || (b == x86_64_mode_x87))
        return x86_64_mode_memory;
    else
        return x86_64_mode_sse;
}

static X86_64_Mode classify_x86_64_inner(CType *ty)
{
    X86_64_Mode mode;
    Sym *f;
    
    switch (ty->t & VT_BTYPE) {
    case VT_VOID: return x86_64_mode_none;
    
    case VT_INT:
    case VT_BYTE:
    case VT_SHORT:
    case VT_LLONG:
    case VT_BOOL:
    case VT_PTR:
    case VT_FUNC:
    case VT_ENUM: return x86_64_mode_integer;
    
    case VT_FLOAT:
    case VT_DOUBLE: return x86_64_mode_sse;
    
    case VT_LDOUBLE: return x86_64_mode_x87;
      
    case VT_STRUCT:
        f = ty->ref;

        // Detect union
        if (f->next && (f->c == f->next->c))
          return x86_64_mode_memory;
        
        mode = x86_64_mode_none;
        for (; f; f = f->next)
            mode = classify_x86_64_merge(mode, classify_x86_64_inner(&f->type));
        
        return mode;
    }
    
    assert(0);
}

static X86_64_Mode classify_x86_64_arg(CType *ty, CType *ret, int *psize, int *palign, int *reg_count)
{
    X86_64_Mode mode;
    int size, align, ret_t = 0;
    
    if (ty->t & (VT_BITFIELD|VT_ARRAY)) {
        *psize = 8;
        *palign = 8;
        *reg_count = 1;
        ret_t = ty->t;
        mode = x86_64_mode_integer;
    } else {
        size = type_size(ty, &align);
        *psize = (size + 7) & ~7;
        *palign = (align + 7) & ~7;
    
        if (size > 16) {
            mode = x86_64_mode_memory;
        } else {
            mode = classify_x86_64_inner(ty);
            switch (mode) {
            case x86_64_mode_integer:
                if (size > 8) {
                    *reg_count = 2;
                    ret_t = VT_QLONG;
                } else {
                    *reg_count = 1;
                    ret_t = (size > 4) ? VT_LLONG : VT_INT;
                }
                break;
                
            case x86_64_mode_x87:
                *reg_count = 1;
                ret_t = VT_LDOUBLE;
                break;

            case x86_64_mode_sse:
                if (size > 8) {
                    *reg_count = 2;
                    ret_t = VT_QFLOAT;
                } else {
                    *reg_count = 1;
                    ret_t = (size > 4) ? VT_DOUBLE : VT_FLOAT;
                }
                break;
            default: break; /* nothing to be done for x86_64_mode_memory and x86_64_mode_none*/
            }
        }
    }
    
    if (ret) {
        ret->ref = NULL;
        ret->t = ret_t;
    }
    
    return mode;
}

ST_FUNC int classify_x86_64_va_arg(CType *ty)
{
    /* This definition must be synced with stdarg.h */
    enum __va_arg_type {
        __va_gen_reg, __va_float_reg, __va_stack
    };
    int size, align, reg_count;
    X86_64_Mode mode = classify_x86_64_arg(ty, NULL, &size, &align, &reg_count);
    switch (mode) {
    default: return __va_stack;
    case x86_64_mode_integer: return __va_gen_reg;
    case x86_64_mode_sse: return __va_float_reg;
    }
}

/* Return the number of registers needed to return the struct, or 0 if
   returning via struct pointer. */
ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret, int *ret_align)
{
    int size, align, reg_count;
    *ret_align = 1; // Never have to re-align return values for x86-64
    return (classify_x86_64_arg(vt, ret, &size, &align, &reg_count) != x86_64_mode_memory);
}

#define REGN 6
static const uint8_t arg_regs[REGN] = {
    TREG_RDI, TREG_RSI, TREG_RDX, TREG_RCX, TREG_R8, TREG_R9
};

static int arg_prepare_reg(int idx) {
  if (idx == 2 || idx == 3)
      /* idx=2: r10, idx=3: r11 */
      return idx + 8;
  else
      return arg_regs[idx];
}

/* Generate function call. The function address is pushed first, then
   all the parameters in call order. This functions pops all the
   parameters and the function address. */
void gfunc_call(TCCState *tcc_state, int nb_args)
{
    X86_64_Mode mode;
    CType type;
    int size, align, r, args_size, stack_adjust, run_start, run_end, i, reg_count;
    int nb_reg_args = 0;
    int nb_sse_args = 0;
    int sse_reg, gen_reg;

    /* calculate the number of integer/float register arguments */
    for(i = 0; i < nb_args; i++) {
        mode = classify_x86_64_arg(&tcc_state->tccgen_vtop[-i].type, NULL, &size, &align, &reg_count);
        if (mode == x86_64_mode_sse)
            nb_sse_args += reg_count;
        else if (mode == x86_64_mode_integer)
            nb_reg_args += reg_count;
    }

    /* arguments are collected in runs. Each run is a collection of 8-byte aligned arguments
       and ended by a 16-byte aligned argument. This is because, from the point of view of
       the callee, argument alignment is computed from the bottom up. */
    /* for struct arguments, we need to call memcpy and the function
       call breaks register passing arguments we are preparing.
       So, we process arguments which will be passed by stack first. */
    gen_reg = nb_reg_args;
    sse_reg = nb_sse_args;
    run_start = 0;
    args_size = 0;
    while (run_start != nb_args) {
        int run_gen_reg = gen_reg, run_sse_reg = sse_reg;
        
        run_end = nb_args;
        stack_adjust = 0;
        for(i = run_start; (i < nb_args) && (run_end == nb_args); i++) {
            mode = classify_x86_64_arg(&tcc_state->tccgen_vtop[-i].type, NULL, &size, &align, &reg_count);
            switch (mode) {
            case x86_64_mode_memory:
            case x86_64_mode_x87:
            stack_arg:
                if (align == 16)
                    run_end = i;
                else
                    stack_adjust += size;
                break;
                
            case x86_64_mode_sse:
                sse_reg -= reg_count;
                if (sse_reg + reg_count > 8) goto stack_arg;
                break;
            
            case x86_64_mode_integer:
                gen_reg -= reg_count;
                if (gen_reg + reg_count > REGN) goto stack_arg;
                break;
	    default: break; /* nothing to be done for x86_64_mode_none */
            }
        }
        
        gen_reg = run_gen_reg;
        sse_reg = run_sse_reg;
        
        /* adjust stack to align SSE boundary */
        if (stack_adjust &= 15) {
            /* fetch cpu flag before the following sub will change the value */
            if (tcc_state->tccgen_vtop >= vstack && (tcc_state->tccgen_vtop->r & VT_VALMASK) == VT_CMP)
                gv(tcc_state, RC_INT);

            stack_adjust = 16 - stack_adjust;
            o(tcc_state, 0x48);
            oad(tcc_state, 0xec81, stack_adjust); /* sub $xxx, %rsp */
            args_size += stack_adjust;
        }
        
        for(i = run_start; i < run_end;) {
            /* Swap argument to top, it will possibly be changed here,
              and might use more temps. At the end of the loop we keep
              in on the stack and swap it back to its original position
              if it is a register. */
            SValue tmp = tcc_state->tccgen_vtop[0];
            tcc_state->tccgen_vtop[0] = tcc_state->tccgen_vtop[-i];
            tcc_state->tccgen_vtop[-i] = tmp;
            
            mode = classify_x86_64_arg(&tcc_state->tccgen_vtop->type, NULL, &size, &align, &reg_count);
            
            int arg_stored = 1;
            switch (tcc_state->tccgen_vtop->type.t & VT_BTYPE) {
            case VT_STRUCT:
                if (mode == x86_64_mode_sse) {
                    if (sse_reg > 8)
                        sse_reg -= reg_count;
                    else
                        arg_stored = 0;
                } else if (mode == x86_64_mode_integer) {
                    if (gen_reg > REGN)
                        gen_reg -= reg_count;
                    else
                        arg_stored = 0;
                }
                
                if (arg_stored) {
                    /* allocate the necessary size on stack */
                    o(tcc_state, 0x48);
                    oad(tcc_state, 0xec81, size); /* sub $xxx, %rsp */
                    /* generate structure store */
                    r = get_reg(tcc_state, RC_INT);
                    orex(tcc_state, 1, r, 0, 0x89); /* mov %rsp, r */
                    o(tcc_state, 0xe0 + REG_VALUE(r));
                    vset(tcc_state, &tcc_state->tccgen_vtop->type, r | VT_LVAL, 0);
                    vswap(tcc_state);
                    vstore(tcc_state);
                    args_size += size;
                }
                break;
                
            case VT_LDOUBLE:
                assert(0);
                break;
                
            case VT_FLOAT:
            case VT_DOUBLE:
                assert(mode == x86_64_mode_sse);
                if (sse_reg > 8) {
                    --sse_reg;
                    r = gv(tcc_state, RC_FLOAT);
                    o(tcc_state, 0x50); /* push $rax */
                    /* movq %xmmN, (%rsp) */
                    o(tcc_state, 0xd60f66);
                    o(tcc_state, 0x04 + REG_VALUE(r)*8);
                    o(tcc_state, 0x24);
                    args_size += size;
                } else {
                    arg_stored = 0;
                }
                break;
                
            default:
                assert(mode == x86_64_mode_integer);
                /* simple type */
                /* XXX: implicit cast ? */
                if (gen_reg > REGN) {
                    --gen_reg;
                    r = gv(tcc_state, RC_INT);
                    orex(tcc_state, 0, r, 0, 0x50 + REG_VALUE(r)); /* push r */
                    args_size += size;
                } else {
                    arg_stored = 0;
                }
                break;
            }
            
            /* And swap the argument back to it's original position.  */
            tmp = tcc_state->tccgen_vtop[0];
            tcc_state->tccgen_vtop[0] = tcc_state->tccgen_vtop[-i];
            tcc_state->tccgen_vtop[-i] = tmp;

            if (arg_stored) {
              vrotb(tcc_state, i+1);
              assert((tcc_state->tccgen_vtop->type.t == tmp.type.t) && (tcc_state->tccgen_vtop->r == tmp.r));
              vpop(tcc_state);
              --nb_args;
              --run_end;
            } else {
              ++i;
            }
        }

        /* handle 16 byte aligned arguments at end of run */
        run_start = i = run_end;
        while (i < nb_args) {
            /* Rotate argument to top since it will always be popped */
            mode = classify_x86_64_arg(&tcc_state->tccgen_vtop[-i].type, NULL, &size, &align, &reg_count);
            if (align != 16)
              break;

            vrotb(tcc_state, i+1);
            
            if ((tcc_state->tccgen_vtop->type.t & VT_BTYPE) == VT_LDOUBLE) {
                gv(tcc_state, RC_ST0);
                oad(tcc_state, 0xec8148, size); /* sub $xxx, %rsp */
                o(tcc_state, 0x7cdb); /* fstpt 0(%rsp) */
                g(tcc_state, 0x24);
                g(tcc_state, 0x00);
                args_size += size;
            } else {
                assert(mode == x86_64_mode_memory);

                /* allocate the necessary size on stack */
                o(tcc_state, 0x48);
                oad(tcc_state, 0xec81, size); /* sub $xxx, %rsp */
                /* generate structure store */
                r = get_reg(tcc_state, RC_INT);
                orex(tcc_state, 1, r, 0, 0x89); /* mov %rsp, r */
                o(tcc_state, 0xe0 + REG_VALUE(r));
                vset(tcc_state, &tcc_state->tccgen_vtop->type, r | VT_LVAL, 0);
                vswap(tcc_state);
                vstore(tcc_state);
                args_size += size;
            }
            
            vpop(tcc_state);
            --nb_args;
        }
    }
    
    /* XXX This should be superfluous.  */
    save_regs(tcc_state, 0); /* save used temporary registers */

    /* then, we prepare register passing arguments.
       Note that we cannot set RDX and RCX in this loop because gv()
       may break these temporary registers. Let's use R10 and R11
       instead of them */
    assert(gen_reg <= REGN);
    assert(sse_reg <= 8);
    for(i = 0; i < nb_args; i++) {
        mode = classify_x86_64_arg(&tcc_state->tccgen_vtop->type, &type, &size, &align, &reg_count);
        /* Alter stack entry type so that gv() knows how to treat it */
        tcc_state->tccgen_vtop->type = type;
        if (mode == x86_64_mode_sse) {
            if (reg_count == 2) {
                sse_reg -= 2;
                gv(tcc_state, RC_FRET); /* Use pair load into xmm0 & xmm1 */
                if (sse_reg) { /* avoid redundant movaps %xmm0, %xmm0 */
                    /* movaps %xmm0, %xmmN */
                    o(tcc_state, 0x280f);
                    o(tcc_state, 0xc0 + (sse_reg << 3));
                    /* movaps %xmm1, %xmmN */
                    o(tcc_state, 0x280f);
                    o(tcc_state, 0xc1 + ((sse_reg+1) << 3));
                }
            } else {
                assert(reg_count == 1);
                --sse_reg;
                /* Load directly to register */
                gv(tcc_state, RC_XMM0 << sse_reg);
            }
        } else if (mode == x86_64_mode_integer) {
            /* simple type */
            /* XXX: implicit cast ? */
            gen_reg -= reg_count;
            r = gv(tcc_state, RC_INT);
            int d = arg_prepare_reg(gen_reg);
            orex(tcc_state, 1, d, r, 0x89); /* mov */
            o(tcc_state, 0xc0 + REG_VALUE(r) * 8 + REG_VALUE(d));
            if (reg_count == 2) {
                d = arg_prepare_reg(gen_reg+1);
                orex(tcc_state, 1, d, tcc_state->tccgen_vtop->r2, 0x89); /* mov */
                o(tcc_state, 0xc0 + REG_VALUE(tcc_state->tccgen_vtop->r2) * 8 + REG_VALUE(d));
            }
        }
        tcc_state->tccgen_vtop--;
    }
    assert(gen_reg == 0);
    assert(sse_reg == 0);

    /* We shouldn't have many operands on the stack anymore, but the
       call address itself is still there, and it might be in %eax
       (or edx/ecx) currently, which the below writes would clobber.
       So evict all remaining operands here.  */
    save_regs(tcc_state, 0);

    /* Copy R10 and R11 into RDX and RCX, respectively */
    if (nb_reg_args > 2) {
        o(tcc_state, 0xd2894c); /* mov %r10, %rdx */
        if (nb_reg_args > 3) {
            o(tcc_state, 0xd9894c); /* mov %r11, %rcx */
        }
    }

    oad(tcc_state, 0xb8, nb_sse_args < 8 ? nb_sse_args : 8); /* mov nb_sse_args, %eax */
    gcall_or_jmp(tcc_state, 0);
    if (args_size)
        gadd_sp(tcc_state, args_size);
    tcc_state->tccgen_vtop--;
}


#define FUNC_PROLOG_SIZE 11

static void push_arg_reg(TCCState* tcc_state, int i) {
    tcc_state->tccgen_loc -= 8;
    gen_modrm64(tcc_state, 0x89, arg_regs[i], VT_LOCAL, NULL, tcc_state->tccgen_loc);
}

/* generate function prolog of type 't' */
void gfunc_prolog(TCCState* tcc_state, CType *func_type)
{
    X86_64_Mode mode;
    int i, addr, align, size, reg_count;
    int param_addr = 0, reg_param_index, sse_param_index;
    Sym *sym;
    CType *type;

    sym = func_type->ref;
    addr = PTR_SIZE * 2;
    tcc_state->tccgen_loc = 0;
    tcc_state->tccgen_ind += FUNC_PROLOG_SIZE;
    tcc_state->func_sub_sp_offset = tcc_state->tccgen_ind;
    tcc_state->func_ret_sub = 0;

    if (func_type->ref->c == FUNC_ELLIPSIS) {
        int seen_reg_num, seen_sse_num, seen_stack_size;
        seen_reg_num = seen_sse_num = 0;
        /* frame pointer and return address */
        seen_stack_size = PTR_SIZE * 2;
        /* count the number of seen parameters */
        sym = func_type->ref;
        while ((sym = sym->next) != NULL) {
            type = &sym->type;
            mode = classify_x86_64_arg(type, NULL, &size, &align, &reg_count);
            switch (mode) {
            default:
            stack_arg:
                seen_stack_size = ((seen_stack_size + align - 1) & -align) + size;
                break;
                
            case x86_64_mode_integer:
                if (seen_reg_num + reg_count <= 8) {
                    seen_reg_num += reg_count;
                } else {
                    seen_reg_num = 8;
                    goto stack_arg;
                }
                break;
                
            case x86_64_mode_sse:
                if (seen_sse_num + reg_count <= 8) {
                    seen_sse_num += reg_count;
                } else {
                    seen_sse_num = 8;
                    goto stack_arg;
                }
                break;
            }
        }

        tcc_state->tccgen_loc -= 16;
        /* movl $0x????????, -0x10(%rbp) */
        o(tcc_state, 0xf045c7);
        gen_le32(tcc_state, seen_reg_num * 8);
        /* movl $0x????????, -0xc(%rbp) */
        o(tcc_state, 0xf445c7);
        gen_le32(tcc_state, seen_sse_num * 16 + 48);
        /* movl $0x????????, -0x8(%rbp) */
        o(tcc_state, 0xf845c7);
        gen_le32(tcc_state, seen_stack_size);

        /* save all register passing arguments */
        for (i = 0; i < 8; i++) {
            tcc_state->tccgen_loc -= 16;
            o(tcc_state, 0xd60f66); /* movq */
            gen_modrm(tcc_state, 7 - i, VT_LOCAL, NULL, tcc_state->tccgen_loc);
            /* movq $0, loc+8(%rbp) */
            o(tcc_state, 0x85c748);
            gen_le32(tcc_state, tcc_state->tccgen_loc + 8);
            gen_le32(tcc_state, 0);
        }
        for (i = 0; i < REGN; i++) {
            push_arg_reg(tcc_state, REGN-1-i);
        }
    }

    sym = func_type->ref;
    reg_param_index = 0;
    sse_param_index = 0;

    /* if the function returns a structure, then add an
       implicit pointer parameter */
    tcc_state->tccgen_func_vt = sym->type;
    mode = classify_x86_64_arg(&tcc_state->tccgen_func_vt, NULL, &size, &align, &reg_count);
    if (mode == x86_64_mode_memory) {
        push_arg_reg(tcc_state, reg_param_index);
        tcc_state->tccgen_func_vc = tcc_state->tccgen_loc;
        reg_param_index++;
    }
    /* define parameters */
    while ((sym = sym->next) != NULL) {
        type = &sym->type;
        mode = classify_x86_64_arg(type, NULL, &size, &align, &reg_count);
        switch (mode) {
        case x86_64_mode_sse:
            if (sse_param_index + reg_count <= 8) {
                /* save arguments passed by register */
                tcc_state->tccgen_loc -= reg_count * 8;
                param_addr = tcc_state->tccgen_loc;
                for (i = 0; i < reg_count; ++i) {
                    o(tcc_state, 0xd60f66); /* movq */
                    gen_modrm(tcc_state, sse_param_index, VT_LOCAL, NULL, param_addr + i*8);
                    ++sse_param_index;
                }
            } else {
                addr = (addr + align - 1) & -align;
                param_addr = addr;
                addr += size;
                sse_param_index += reg_count;
            }
            break;
            
        case x86_64_mode_memory:
        case x86_64_mode_x87:
            addr = (addr + align - 1) & -align;
            param_addr = addr;
            addr += size;
            break;
            
        case x86_64_mode_integer: {
            if (reg_param_index + reg_count <= REGN) {
                /* save arguments passed by register */
                tcc_state->tccgen_loc -= reg_count * 8;
                param_addr = tcc_state->tccgen_loc;
                for (i = 0; i < reg_count; ++i) {
                    gen_modrm64(tcc_state, 0x89, arg_regs[reg_param_index], VT_LOCAL, NULL, param_addr + i*8);
                    ++reg_param_index;
                }
            } else {
                addr = (addr + align - 1) & -align;
                param_addr = addr;
                addr += size;
                reg_param_index += reg_count;
            }
            break;
        }
	default: break; /* nothing to be done for x86_64_mode_none */
        }
        sym_push(tcc_state, sym->v & ~SYM_FIELD, type,
                 VT_LOCAL | VT_LVAL, param_addr);
    }
}

/* generate function epilog */
void gfunc_epilog(TCCState* tcc_state)
{
    int v, saved_ind;

    o(tcc_state, 0xc9); /* leave */
    if (tcc_state->func_ret_sub == 0) {
        o(tcc_state, 0xc3); /* ret */
    } else {
        o(tcc_state, 0xc2); /* ret n */
        g(tcc_state, tcc_state->func_ret_sub);
        g(tcc_state, tcc_state->func_ret_sub >> 8);
    }
    /* align local size to word & save local variables */
    v = (-tcc_state->tccgen_loc + 15) & -16;
    saved_ind = tcc_state->tccgen_ind;
    tcc_state->tccgen_ind = tcc_state->func_sub_sp_offset - FUNC_PROLOG_SIZE;
    o(tcc_state, 0xe5894855);  /* push %rbp, mov %rsp, %rbp */
    o(tcc_state, 0xec8148);  /* sub rsp, stacksize */
    gen_le32(tcc_state, v);
    tcc_state->tccgen_ind = saved_ind;
}

#endif /* not PE */

/* generate a jump to a label */
int gjmp(TCCState* tcc_state, int t)
{
    return psym(tcc_state, 0xe9, t);
}

/* generate a jump to a fixed address */
void gjmp_addr(TCCState* tcc_state, int a)
{
    int r;
    r = a - tcc_state->tccgen_ind - 2;
    if (r == (char)r) {
        g(tcc_state, 0xeb);
        g(tcc_state, r);
    } else {
        oad(tcc_state, 0xe9, a - tcc_state->tccgen_ind - 5);
    }
}

/* generate a test. set 'inv' to invert test. Stack entry is popped */
int gtst(TCCState* tcc_state, int inv, int t)
{
    int v, *p;

    v = tcc_state->tccgen_vtop->r & VT_VALMASK;
    if (v == VT_CMP) {
        /* fast case : can jump directly since flags are set */
	if (tcc_state->tccgen_vtop->c.i & 0x100)
	  {
	    /* This was a float compare.  If the parity flag is set
	       the result was unordered.  For anything except != this
	       means false and we don't jump (anding both conditions).
	       For != this means true (oring both).
	       Take care about inverting the test.  We need to jump
	       to our target if the result was unordered and test wasn't NE,
	       otherwise if unordered we don't want to jump.  */
	    tcc_state->tccgen_vtop->c.i &= ~0x100;
	    if (!inv == (tcc_state->tccgen_vtop->c.i != TOK_NE))
	      o(tcc_state, 0x067a);  /* jp +6 */
	    else
	      {
	        g(tcc_state, 0x0f);
		t = psym(tcc_state, 0x8a, t); /* jp t */
	      }
	  }
        g(tcc_state, 0x0f);
        t = psym(tcc_state, (tcc_state->tccgen_vtop->c.i - 16) ^ inv, t);
    } else { /* VT_JMP || VT_JMPI */
        /* && or || optimization */
        if ((v & 1) == inv) {
            /* insert vtop->c jump list in t */
            p = &tcc_state->tccgen_vtop->c.i;
            while (*p != 0)
                p = (int *)(tcc_state->tccgen_cur_text_section->data + *p);
            *p = t;
            t = tcc_state->tccgen_vtop->c.i;
        } else {
            t = gjmp(tcc_state, t);
            gsym(tcc_state, tcc_state->tccgen_vtop->c.i);
        }
    }
    tcc_state->tccgen_vtop--;
    return t;
}

/* generate an integer binary operation */
void gen_opi(TCCState *tcc_state, int op)
{
    int r, fr, opc, c;
    int ll, uu, cc;

    ll = is64_type(tcc_state->tccgen_vtop[-1].type.t);
    uu = (tcc_state->tccgen_vtop[-1].type.t & VT_UNSIGNED) != 0;
    cc = (tcc_state->tccgen_vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;

    switch(op) {
    case '+':
    case TOK_ADDC1: /* add with carry generation */
        opc = 0;
    gen_op8:
        if (cc && (!ll || (int)tcc_state->tccgen_vtop->c.ll == tcc_state->tccgen_vtop->c.ll)) {
            /* constant case */
            vswap(tcc_state);
            r = gv(tcc_state, RC_INT);
            vswap(tcc_state);
            c = tcc_state->tccgen_vtop->c.i;
            if (c == (char)c) {
                /* XXX: generate inc and dec for smaller code ? */
                orex(tcc_state, ll, r, 0, 0x83);
                o(tcc_state, 0xc0 | (opc << 3) | REG_VALUE(r));
                g(tcc_state, c);
            } else {
                orex(tcc_state, ll, r, 0, 0x81);
                oad(tcc_state, 0xc0 | (opc << 3) | REG_VALUE(r), c);
            }
        } else {
            gv2(tcc_state, RC_INT, RC_INT);
            r = tcc_state->tccgen_vtop[-1].r;
            fr = tcc_state->tccgen_vtop[0].r;
            orex(tcc_state, ll, r, fr, (opc << 3) | 0x01);
            o(tcc_state, 0xc0 + REG_VALUE(r) + REG_VALUE(fr) * 8);
        }
        tcc_state->tccgen_vtop--;
        if (op >= TOK_ULT && op <= TOK_GT) {
            tcc_state->tccgen_vtop->r = VT_CMP;
            tcc_state->tccgen_vtop->c.i = op;
        }
        break;
    case '-':
    case TOK_SUBC1: /* sub with carry generation */
        opc = 5;
        goto gen_op8;
    case TOK_ADDC2: /* add with carry use */
        opc = 2;
        goto gen_op8;
    case TOK_SUBC2: /* sub with carry use */
        opc = 3;
        goto gen_op8;
    case '&':
        opc = 4;
        goto gen_op8;
    case '^':
        opc = 6;
        goto gen_op8;
    case '|':
        opc = 1;
        goto gen_op8;
    case '*':
        gv2(tcc_state, RC_INT, RC_INT);
        r = tcc_state->tccgen_vtop[-1].r;
        fr = tcc_state->tccgen_vtop[0].r;
        orex(tcc_state, ll, fr, r, 0xaf0f); /* imul fr, r */
        o(tcc_state, 0xc0 + REG_VALUE(fr) + REG_VALUE(r) * 8);
        tcc_state->tccgen_vtop--;
        break;
    case TOK_SHL:
        opc = 4;
        goto gen_shift;
    case TOK_SHR:
        opc = 5;
        goto gen_shift;
    case TOK_SAR:
        opc = 7;
    gen_shift:
        opc = 0xc0 | (opc << 3);
        if (cc) {
            /* constant case */
            vswap(tcc_state);
            r = gv(tcc_state, RC_INT);
            vswap(tcc_state);
            orex(tcc_state, ll, r, 0, 0xc1); /* shl/shr/sar $xxx, r */
            o(tcc_state, opc | REG_VALUE(r));
            g(tcc_state, tcc_state->tccgen_vtop->c.i & (ll ? 63 : 31));
        } else {
            /* we generate the shift in ecx */
            gv2(tcc_state, RC_INT, RC_RCX);
            r = tcc_state->tccgen_vtop[-1].r;
            orex(tcc_state, ll, r, 0, 0xd3); /* shl/shr/sar %cl, r */
            o(tcc_state, opc | REG_VALUE(r));
        }
        tcc_state->tccgen_vtop--;
        break;
    case TOK_UDIV:
    case TOK_UMOD:
        uu = 1;
        goto divmod;
    case '/':
    case '%':
    case TOK_PDIV:
        uu = 0;
    divmod:
        /* first operand must be in eax */
        /* XXX: need better constraint for second operand */
        gv2(tcc_state, RC_RAX, RC_RCX);
        r = tcc_state->tccgen_vtop[-1].r;
        fr = tcc_state->tccgen_vtop[0].r;
        tcc_state->tccgen_vtop--;
        save_reg(tcc_state, TREG_RDX);
        orex(tcc_state, ll, 0, 0, uu ? 0xd231 : 0x99); /* xor %edx,%edx : cqto */
        orex(tcc_state, ll, fr, 0, 0xf7); /* div fr, %eax */
        o(tcc_state, (uu ? 0xf0 : 0xf8) + REG_VALUE(fr));
        if (op == '%' || op == TOK_UMOD)
            r = TREG_RDX;
        else
            r = TREG_RAX;
        tcc_state->tccgen_vtop->r = r;
        break;
    default:
        opc = 7;
        goto gen_op8;
    }
}

void gen_opl(TCCState *tcc_state, int op)
{
    gen_opi(tcc_state, op);
}

/* generate a floating point operation 'v = t1 op t2' instruction. The
   two operands are guaranted to have the same floating point type */
/* XXX: need to use ST1 too */
void gen_opf(TCCState *tcc_state, int op)
{
    int a, ft, fc, swapped, r;
    int float_type =
        (tcc_state->tccgen_vtop->type.t & VT_BTYPE) == VT_LDOUBLE ? RC_ST0 : RC_FLOAT;

    /* convert constants to memory references */
    if ((tcc_state->tccgen_vtop[-1].r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
        vswap(tcc_state);
        gv(tcc_state, float_type);
        vswap(tcc_state);
    }
    if ((tcc_state->tccgen_vtop[0].r & (VT_VALMASK | VT_LVAL)) == VT_CONST)
        gv(tcc_state, float_type);

    /* must put at least one value in the floating point register */
    if ((tcc_state->tccgen_vtop[-1].r & VT_LVAL) &&
        (tcc_state->tccgen_vtop[0].r & VT_LVAL)) {
        vswap(tcc_state);
        gv(tcc_state, float_type);
        vswap(tcc_state);
    }
    swapped = 0;
    /* swap the stack if needed so that t1 is the register and t2 is
       the memory reference */
    if (tcc_state->tccgen_vtop[-1].r & VT_LVAL) {
        vswap(tcc_state);
        swapped = 1;
    }
    if ((tcc_state->tccgen_vtop->type.t & VT_BTYPE) == VT_LDOUBLE) {
        if (op >= TOK_ULT && op <= TOK_GT) {
            /* load on stack second operand */
            load(tcc_state, TREG_ST0, tcc_state->tccgen_vtop);
            save_reg(tcc_state, TREG_RAX); /* eax is used by FP comparison code */
            if (op == TOK_GE || op == TOK_GT)
                swapped = !swapped;
            else if (op == TOK_EQ || op == TOK_NE)
                swapped = 0;
            if (swapped)
                o(tcc_state, 0xc9d9); /* fxch %st(1) */
            if (op == TOK_EQ || op == TOK_NE)
                o(tcc_state, 0xe9da); /* fucompp */
            else
                o(tcc_state, 0xd9de); /* fcompp */
            o(tcc_state, 0xe0df); /* fnstsw %ax */
            if (op == TOK_EQ) {
                o(tcc_state, 0x45e480); /* and $0x45, %ah */
                o(tcc_state, 0x40fC80); /* cmp $0x40, %ah */
            } else if (op == TOK_NE) {
                o(tcc_state, 0x45e480); /* and $0x45, %ah */
                o(tcc_state, 0x40f480); /* xor $0x40, %ah */
                op = TOK_NE;
            } else if (op == TOK_GE || op == TOK_LE) {
                o(tcc_state, 0x05c4f6); /* test $0x05, %ah */
                op = TOK_EQ;
            } else {
                o(tcc_state, 0x45c4f6); /* test $0x45, %ah */
                op = TOK_EQ;
            }
            tcc_state->tccgen_vtop--;
            tcc_state->tccgen_vtop->r = VT_CMP;
            tcc_state->tccgen_vtop->c.i = op;
        } else {
            /* no memory reference possible for long double operations */
            load(tcc_state, TREG_ST0, tcc_state->tccgen_vtop);
            swapped = !swapped;

            switch(op) {
            default:
            case '+':
                a = 0;
                break;
            case '-':
                a = 4;
                if (swapped)
                    a++;
                break;
            case '*':
                a = 1;
                break;
            case '/':
                a = 6;
                if (swapped)
                    a++;
                break;
            }
            ft = tcc_state->tccgen_vtop->type.t;
            fc = tcc_state->tccgen_vtop->c.ul;
            o(tcc_state, 0xde); /* fxxxp %st, %st(1) */
            o(tcc_state, 0xc1 + (a << 3));
            tcc_state->tccgen_vtop--;
        }
    } else {
        if (op >= TOK_ULT && op <= TOK_GT) {
            /* if saved lvalue, then we must reload it */
            r = tcc_state->tccgen_vtop->r;
            fc = tcc_state->tccgen_vtop->c.ul;
            if ((r & VT_VALMASK) == VT_LLOCAL) {
                SValue v1;
                r = get_reg(tcc_state, RC_INT);
                v1.type.t = VT_PTR;
                v1.r = VT_LOCAL | VT_LVAL;
                v1.c.ul = fc;
                load(tcc_state, r, &v1);
                fc = 0;
            }

            if (op == TOK_EQ || op == TOK_NE) {
                swapped = 0;
            } else {
                if (op == TOK_LE || op == TOK_LT)
                    swapped = !swapped;
                if (op == TOK_LE || op == TOK_GE) {
                    op = 0x93; /* setae */
                } else {
                    op = 0x97; /* seta */
                }
            }

            if (swapped) {
                gv(tcc_state, RC_FLOAT);
                vswap(tcc_state);
            }
            assert(!(tcc_state->tccgen_vtop[-1].r & VT_LVAL));
            
            if ((tcc_state->tccgen_vtop->type.t & VT_BTYPE) == VT_DOUBLE)
                o(tcc_state, 0x66);
            if (op == TOK_EQ || op == TOK_NE)
                o(tcc_state, 0x2e0f); /* ucomisd */
            else
                o(tcc_state, 0x2f0f); /* comisd */

            if (tcc_state->tccgen_vtop->r & VT_LVAL) {
                gen_modrm(tcc_state, tcc_state->tccgen_vtop[-1].r, r, tcc_state->tccgen_vtop->sym, fc);
            } else {
                o(tcc_state, 0xc0 + REG_VALUE(tcc_state->tccgen_vtop[0].r) + REG_VALUE(tcc_state->tccgen_vtop[-1].r)*8);
            }

            tcc_state->tccgen_vtop--;
            tcc_state->tccgen_vtop->r = VT_CMP;
            tcc_state->tccgen_vtop->c.i = op | 0x100;
        } else {
            assert((tcc_state->tccgen_vtop->type.t & VT_BTYPE) != VT_LDOUBLE);
            switch(op) {
            default:
            case '+':
                a = 0;
                break;
            case '-':
                a = 4;
                break;
            case '*':
                a = 1;
                break;
            case '/':
                a = 6;
                break;
            }
            ft = tcc_state->tccgen_vtop->type.t;
            fc = tcc_state->tccgen_vtop->c.ul;
            assert((ft & VT_BTYPE) != VT_LDOUBLE);
            
            r = tcc_state->tccgen_vtop->r;
            /* if saved lvalue, then we must reload it */
            if ((tcc_state->tccgen_vtop->r & VT_VALMASK) == VT_LLOCAL) {
                SValue v1;
                r = get_reg(tcc_state, RC_INT);
                v1.type.t = VT_PTR;
                v1.r = VT_LOCAL | VT_LVAL;
                v1.c.ul = fc;
                load(tcc_state, r, &v1);
                fc = 0;
            }
            
            assert(!(tcc_state->tccgen_vtop[-1].r & VT_LVAL));
            if (swapped) {
                assert(tcc_state->tccgen_vtop->r & VT_LVAL);
                gv(tcc_state, RC_FLOAT);
                vswap(tcc_state);
            }
            
            if ((ft & VT_BTYPE) == VT_DOUBLE) {
                o(tcc_state, 0xf2);
            } else {
                o(tcc_state, 0xf3);
            }
            o(tcc_state, 0x0f);
            o(tcc_state, 0x58 + a);
            
            if (tcc_state->tccgen_vtop->r & VT_LVAL) {
                gen_modrm(tcc_state, tcc_state->tccgen_vtop[-1].r, r, tcc_state->tccgen_vtop->sym, fc);
            } else {
                o(tcc_state, 0xc0 + REG_VALUE(tcc_state->tccgen_vtop[0].r) + REG_VALUE(tcc_state->tccgen_vtop[-1].r)*8);
            }

            tcc_state->tccgen_vtop--;
        }
    }
}

/* convert integers to fp 't' type. Must handle 'int', 'unsigned int'
   and 'long long' cases. */
void gen_cvt_itof(TCCState *tcc_state, int t)
{
    if ((t & VT_BTYPE) == VT_LDOUBLE) {
        save_reg(tcc_state, TREG_ST0);
        gv(tcc_state, RC_INT);
        if ((tcc_state->tccgen_vtop->type.t & VT_BTYPE) == VT_LLONG) {
            /* signed long long to float/double/long double (unsigned case
               is handled generically) */
            o(tcc_state, 0x50 + (tcc_state->tccgen_vtop->r & VT_VALMASK)); /* push r */
            o(tcc_state, 0x242cdf); /* fildll (%rsp) */
            o(tcc_state, 0x08c48348); /* add $8, %rsp */
        } else if ((tcc_state->tccgen_vtop->type.t & (VT_BTYPE | VT_UNSIGNED)) ==
                   (VT_INT | VT_UNSIGNED)) {
            /* unsigned int to float/double/long double */
            o(tcc_state, 0x6a); /* push $0 */
            g(tcc_state, 0x00);
            o(tcc_state, 0x50 + (tcc_state->tccgen_vtop->r & VT_VALMASK)); /* push r */
            o(tcc_state, 0x242cdf); /* fildll (%rsp) */
            o(tcc_state, 0x10c48348); /* add $16, %rsp */
        } else {
            /* int to float/double/long double */
            o(tcc_state, 0x50 + (tcc_state->tccgen_vtop->r & VT_VALMASK)); /* push r */
            o(tcc_state, 0x2404db); /* fildl (%rsp) */
            o(tcc_state, 0x08c48348); /* add $8, %rsp */
        }
        tcc_state->tccgen_vtop->r = TREG_ST0;
    } else {
        int r = get_reg(tcc_state, RC_FLOAT);
        gv(tcc_state, RC_INT);
        o(tcc_state, 0xf2 + ((t & VT_BTYPE) == VT_FLOAT?1:0));
        if ((tcc_state->tccgen_vtop->type.t & (VT_BTYPE | VT_UNSIGNED)) ==
            (VT_INT | VT_UNSIGNED) ||
            (tcc_state->tccgen_vtop->type.t & VT_BTYPE) == VT_LLONG) {
            o(tcc_state, 0x48); /* REX */
        }
        o(tcc_state, 0x2a0f);
        o(tcc_state, 0xc0 + (tcc_state->tccgen_vtop->r & VT_VALMASK) + REG_VALUE(r)*8); /* cvtsi2sd */
        tcc_state->tccgen_vtop->r = r;
    }
}

/* convert from one floating point type to another */
void gen_cvt_ftof(TCCState *tcc_state, int t)
{
    int ft, bt, tbt;

    ft = tcc_state->tccgen_vtop->type.t;
    bt = ft & VT_BTYPE;
    tbt = t & VT_BTYPE;
    
    if (bt == VT_FLOAT) {
        gv(tcc_state, RC_FLOAT);
        if (tbt == VT_DOUBLE) {
            o(tcc_state, 0x140f); /* unpcklps */
            o(tcc_state, 0xc0 + REG_VALUE(tcc_state->tccgen_vtop->r)*9);
            o(tcc_state, 0x5a0f); /* cvtps2pd */
            o(tcc_state, 0xc0 + REG_VALUE(tcc_state->tccgen_vtop->r)*9);
        } else if (tbt == VT_LDOUBLE) {
            save_reg(tcc_state, RC_ST0);
            /* movss %xmm0,-0x10(%rsp) */
            o(tcc_state, 0x110ff3);
            o(tcc_state, 0x44 + REG_VALUE(tcc_state->tccgen_vtop->r)*8);
            o(tcc_state, 0xf024);
            o(tcc_state, 0xf02444d9); /* flds -0x10(%rsp) */
            tcc_state->tccgen_vtop->r = TREG_ST0;
        }
    } else if (bt == VT_DOUBLE) {
        gv(tcc_state, RC_FLOAT);
        if (tbt == VT_FLOAT) {
            o(tcc_state, 0x140f66); /* unpcklpd */
            o(tcc_state, 0xc0 + REG_VALUE(tcc_state->tccgen_vtop->r)*9);
            o(tcc_state, 0x5a0f66); /* cvtpd2ps */
            o(tcc_state, 0xc0 + REG_VALUE(tcc_state->tccgen_vtop->r)*9);
        } else if (tbt == VT_LDOUBLE) {
            save_reg(tcc_state, RC_ST0);
            /* movsd %xmm0,-0x10(%rsp) */
            o(tcc_state, 0x110ff2);
            o(tcc_state, 0x44 + REG_VALUE(tcc_state->tccgen_vtop->r)*8);
            o(tcc_state, 0xf024);
            o(tcc_state, 0xf02444dd); /* fldl -0x10(%rsp) */
            tcc_state->tccgen_vtop->r = TREG_ST0;
        }
    } else {
        int r;
        gv(tcc_state, RC_ST0);
        r = get_reg(tcc_state, RC_FLOAT);
        if (tbt == VT_DOUBLE) {
            o(tcc_state, 0xf0245cdd); /* fstpl -0x10(%rsp) */
            /* movsd -0x10(%rsp),%xmm0 */
            o(tcc_state, 0x100ff2);
            o(tcc_state, 0x44 + REG_VALUE(r)*8);
            o(tcc_state, 0xf024);
            tcc_state->tccgen_vtop->r = r;
        } else if (tbt == VT_FLOAT) {
            o(tcc_state, 0xf0245cd9); /* fstps -0x10(%rsp) */
            /* movss -0x10(%rsp),%xmm0 */
            o(tcc_state, 0x100ff3);
            o(tcc_state, 0x44 + REG_VALUE(r)*8);
            o(tcc_state, 0xf024);
            tcc_state->tccgen_vtop->r = r;
        }
    }
}

/* convert fp to int 't' type */
void gen_cvt_ftoi(TCCState *tcc_state, int t)
{
    int ft, bt, size, r;
    ft = tcc_state->tccgen_vtop->type.t;
    bt = ft & VT_BTYPE;
    if (bt == VT_LDOUBLE) {
        gen_cvt_ftof(tcc_state, VT_DOUBLE);
        bt = VT_DOUBLE;
    }

    gv(tcc_state, RC_FLOAT);
    if (t != VT_INT)
        size = 8;
    else
        size = 4;

    r = get_reg(tcc_state, RC_INT);
    if (bt == VT_FLOAT) {
        o(tcc_state, 0xf3);
    } else if (bt == VT_DOUBLE) {
        o(tcc_state, 0xf2);
    } else {
        assert(0);
    }
    orex(tcc_state, size == 8, r, 0, 0x2c0f); /* cvttss2si or cvttsd2si */
    o(tcc_state, 0xc0 + REG_VALUE(tcc_state->tccgen_vtop->r) + REG_VALUE(r)*8);
    tcc_state->tccgen_vtop->r = r;
}

/* computed goto support */
void ggoto(TCCState* tcc_state)
{
    gcall_or_jmp(tcc_state, 1);
    tcc_state->tccgen_vtop--;
}

/* Save the stack pointer onto the stack and return the location of its address */
ST_FUNC void gen_vla_sp_save(TCCState *tcc_state, int addr) {
    /* mov %rsp,addr(%rbp)*/
    gen_modrm64(tcc_state, 0x89, TREG_RSP, VT_LOCAL, NULL, addr);
}

/* Restore the SP from a location on the stack */
ST_FUNC void gen_vla_sp_restore(TCCState *tcc_state, int addr) {
    gen_modrm64(tcc_state, 0x8b, TREG_RSP, VT_LOCAL, NULL, addr);
}

/* Subtract from the stack pointer, and push the resulting value onto the stack */
ST_FUNC void gen_vla_alloc(TCCState *tcc_state, CType *type, int align) {
#ifdef TCC_TARGET_PE
    /* alloca does more than just adjust %rsp on Windows */
    vpush_global_sym(tcc_state, &tcc_state->tccgen_func_old_type, TOK_alloca);
    vswap(tcc_state); /* Move alloca ref past allocation size */
    gfunc_call(tcc_state, 1);
    vset(tcc_state, type, REG_IRET, 0);
#else
    int r;
    r = gv(tcc_state, RC_INT); /* allocation size */
    /* sub r,%rsp */
    o(tcc_state, 0x2b48);
    o(tcc_state, 0xe0 | REG_VALUE(r));
    /* We align to 16 bytes rather than align */
    /* and ~15, %rsp */
    o(tcc_state, 0xf0e48348);
    /* mov %rsp, r */
    o(tcc_state, 0x8948);
    o(tcc_state, 0xe0 | REG_VALUE(r));
    vpop(tcc_state);
    vset(tcc_state, type, r, 0);
#endif
}


/* end of x86-64 code generator */
/*************************************************************/
#endif /* ! TARGET_DEFS_ONLY */
/******************************************************/
