/*
 *  GAS like assembler for TCC
 * 
 *  Copyright (c) 2001-2004 Fabrice Bellard
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

#define USING_GLOBALS
#include "tcc.h"
#ifdef CONFIG_TCC_ASM

ST_FUNC int asm_get_local_label_name(TCCState *S, unsigned int n)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "L..%u", n);
    return tok_alloc_const(S, buf);
}

static int tcc_assemble_internal(TCCState *S, int do_preprocess, int global);
static Sym* asm_new_label(TCCState *S, int label, int is_local);
static Sym* asm_new_label1(TCCState *S, int label, int is_local, int sh_num, int value);

/* If a C name has an _ prepended then only asm labels that start
   with _ are representable in C, by removing the first _.  ASM names
   without _ at the beginning don't correspond to C names, but we use
   the global C symbol table to track ASM names as well, so we need to
   transform those into ones that don't conflict with a C name,
   so prepend a '.' for them, but force the ELF asm name to be set.  */
static int asm2cname(TCCState *S, int v, int *addeddot)
{
    const char *name;
    *addeddot = 0;
    if (!S->leading_underscore)
      return v;
    name = get_tok_str(S, v, NULL);
    if (!name)
      return v;
    if (name[0] == '_') {
        v = tok_alloc_const(S, name + 1);
    } else if (!strchr(name, '.')) {
        char newname[256];
        snprintf(newname, sizeof newname, ".%s", name);
        v = tok_alloc_const(S, newname);
        *addeddot = 1;
    }
    return v;
}

static Sym *asm_label_find(TCCState *S, int v)
{
    Sym *sym;
    int addeddot;
    v = asm2cname(S, v, &addeddot);
    sym = sym_find(S, v);
    while (sym && sym->sym_scope && !(sym->type.t & VT_STATIC))
        sym = sym->prev_tok;
    return sym;
}

static Sym *asm_label_push(TCCState *S, int v)
{
    int addeddot, v2 = asm2cname(S, v, &addeddot);
    /* We always add VT_EXTERN, for sym definition that's tentative
       (for .set, removed for real defs), for mere references it's correct
       as is.  */
    Sym *sym = global_identifier_push(S, v2, VT_ASM | VT_EXTERN | VT_STATIC, 0);
    if (addeddot)
        sym->asm_label = v;
    return sym;
}

/* Return a symbol we can use inside the assembler, having name NAME.
   Symbols from asm and C source share a namespace.  If we generate
   an asm symbol it's also a (file-global) C symbol, but it's
   either not accessible by name (like "L.123"), or its type information
   is such that it's not usable without a proper C declaration.

   Sometimes we need symbols accessible by name from asm, which
   are anonymous in C, in this case CSYM can be used to transfer
   all information from that symbol to the (possibly newly created)
   asm symbol.  */
ST_FUNC Sym* get_asm_sym(TCCState *S, int name, Sym *csym)
{
    Sym *sym = asm_label_find(S, name);
    if (!sym) {
	sym = asm_label_push(S, name);
	if (csym)
	  sym->c = csym->c;
    }
    return sym;
}

static Sym* asm_section_sym(TCCState *S, Section *sec)
{
    char buf[100]; int label; Sym *sym;
    snprintf(buf, sizeof buf, "L.%s", sec->name);
    label = tok_alloc_const(S, buf);
    sym = asm_label_find(S, label);
    return sym ? sym : asm_new_label1(S, label, 1, sec->sh_num, 0);
}

/* We do not use the C expression parser to handle symbols. Maybe the
   C expression parser could be tweaked to do so. */

static void asm_expr_unary(TCCState *S, ExprValue *pe)
{
    Sym *sym;
    int op, label;
    uint64_t n;
    const char *p;

    switch(S->tok) {
    case TOK_PPNUM:
        p = S->tokc.str.data;
        n = strtoull(p, (char **)&p, 0);
        if (*p == 'b' || *p == 'f') {
            /* backward or forward label */
            label = asm_get_local_label_name(S, n);
            sym = asm_label_find(S, label);
            if (*p == 'b') {
                /* backward : find the last corresponding defined label */
                if (sym && (!sym->c || elfsym(S, sym)->st_shndx == SHN_UNDEF))
                    sym = sym->prev_tok;
                if (!sym)
                    tcc_error(S, "local label '%d' not found backward", (int)n);
            } else {
                /* forward */
                if (!sym || (sym->c && elfsym(S, sym)->st_shndx != SHN_UNDEF)) {
                    /* if the last label is defined, then define a new one */
		    sym = asm_label_push(S, label);
                }
            }
	    pe->v = 0;
	    pe->sym = sym;
	    pe->pcrel = 0;
        } else if (*p == '\0') {
            pe->v = n;
            pe->sym = NULL;
	    pe->pcrel = 0;
        } else {
            tcc_error(S, "invalid number syntax");
        }
        next(S);
        break;
    case '+':
        next(S);
        asm_expr_unary(S, pe);
        break;
    case '-':
    case '~':
        op = S->tok;
        next(S);
        asm_expr_unary(S, pe);
        if (pe->sym)
            tcc_error(S, "invalid operation with label");
        if (op == '-')
            pe->v = -pe->v;
        else
            pe->v = ~pe->v;
        break;
    case TOK_CCHAR:
    case TOK_LCHAR:
	pe->v = S->tokc.i;
	pe->sym = NULL;
	pe->pcrel = 0;
	next(S);
	break;
    case '(':
        next(S);
        asm_expr(S, pe);
        skip(S, ')');
        break;
    case '.':
        pe->v = S->ind;
        pe->sym = asm_section_sym(S, cur_text_section);
        pe->pcrel = 0;
        next(S);
        break;
    default:
        if (S->tok >= TOK_IDENT) {
	    ElfSym *esym;
            /* label case : if the label was not found, add one */
	    sym = get_asm_sym(S, S->tok, NULL);
	    esym = elfsym(S, sym);
            if (esym && esym->st_shndx == SHN_ABS) {
                /* if absolute symbol, no need to put a symbol value */
                pe->v = esym->st_value;
                pe->sym = NULL;
		pe->pcrel = 0;
            } else {
                pe->v = 0;
                pe->sym = sym;
		pe->pcrel = 0;
            }
            next(S);
        } else {
            tcc_error(S, "bad expression syntax [%s]", get_tok_str(S, S->tok, &S->tokc));
        }
        break;
    }
}
    
static void asm_expr_prod(TCCState *S, ExprValue *pe)
{
    int op;
    ExprValue e2;

    asm_expr_unary(S, pe);
    for(;;) {
        op = S->tok;
        if (op != '*' && op != '/' && op != '%' && 
            op != TOK_SHL && op != TOK_SAR)
            break;
        next(S);
        asm_expr_unary(S, &e2);
        if (pe->sym || e2.sym)
            tcc_error(S, "invalid operation with label");
        switch(op) {
        case '*':
            pe->v *= e2.v;
            break;
        case '/':  
            if (e2.v == 0) {
            div_error:
                tcc_error(S, "division by zero");
            }
            pe->v /= e2.v;
            break;
        case '%':  
            if (e2.v == 0)
                goto div_error;
            pe->v %= e2.v;
            break;
        case TOK_SHL:
            pe->v <<= e2.v;
            break;
        default:
        case TOK_SAR:
            pe->v >>= e2.v;
            break;
        }
    }
}

static void asm_expr_logic(TCCState *S, ExprValue *pe)
{
    int op;
    ExprValue e2;

    asm_expr_prod(S, pe);
    for(;;) {
        op = S->tok;
        if (op != '&' && op != '|' && op != '^')
            break;
        next(S);
        asm_expr_prod(S, &e2);
        if (pe->sym || e2.sym)
            tcc_error(S, "invalid operation with label");
        switch(op) {
        case '&':
            pe->v &= e2.v;
            break;
        case '|':  
            pe->v |= e2.v;
            break;
        default:
        case '^':
            pe->v ^= e2.v;
            break;
        }
    }
}

static inline void asm_expr_sum(TCCState *S, ExprValue *pe)
{
    int op;
    ExprValue e2;

    asm_expr_logic(S, pe);
    for(;;) {
        op = S->tok;
        if (op != '+' && op != '-')
            break;
        next(S);
        asm_expr_logic(S, &e2);
        if (op == '+') {
            if (pe->sym != NULL && e2.sym != NULL)
                goto cannot_relocate;
            pe->v += e2.v;
            if (pe->sym == NULL && e2.sym != NULL)
                pe->sym = e2.sym;
        } else {
            pe->v -= e2.v;
            /* NOTE: we are less powerful than gas in that case
               because we store only one symbol in the expression */
	    if (!e2.sym) {
		/* OK */
	    } else if (pe->sym == e2.sym) { 
		/* OK */
		pe->sym = NULL; /* same symbols can be subtracted to NULL */
	    } else {
		ElfSym *esym1, *esym2;
		esym1 = elfsym(S, pe->sym);
		esym2 = elfsym(S, e2.sym);
		if (esym1 && esym1->st_shndx == esym2->st_shndx
		    && esym1->st_shndx != SHN_UNDEF) {
		    /* we also accept defined symbols in the same section */
		    pe->v += esym1->st_value - esym2->st_value;
		    pe->sym = NULL;
		} else if (esym2->st_shndx == cur_text_section->sh_num) {
		    /* When subtracting a defined symbol in current section
		       this actually makes the value PC-relative.  */
		    pe->v -= esym2->st_value - S->ind - 4;
		    pe->pcrel = 1;
		    e2.sym = NULL;
		} else {
cannot_relocate:
		    tcc_error(S, "invalid operation with label");
		}
	    }
        }
    }
}

static inline void asm_expr_cmp(TCCState *S, ExprValue *pe)
{
    int op;
    ExprValue e2;

    asm_expr_sum(S, pe);
    for(;;) {
        op = S->tok;
	if (op != TOK_EQ && op != TOK_NE
	    && (op > TOK_GT || op < TOK_ULE))
            break;
        next(S);
        asm_expr_sum(S, &e2);
        if (pe->sym || e2.sym)
            tcc_error(S, "invalid operation with label");
        switch(op) {
	case TOK_EQ:
	    pe->v = pe->v == e2.v;
	    break;
	case TOK_NE:
	    pe->v = pe->v != e2.v;
	    break;
	case TOK_LT:
	    pe->v = (int64_t)pe->v < (int64_t)e2.v;
	    break;
	case TOK_GE:
	    pe->v = (int64_t)pe->v >= (int64_t)e2.v;
	    break;
	case TOK_LE:
	    pe->v = (int64_t)pe->v <= (int64_t)e2.v;
	    break;
	case TOK_GT:
	    pe->v = (int64_t)pe->v > (int64_t)e2.v;
	    break;
        default:
            break;
        }
	/* GAS compare results are -1/0 not 1/0.  */
	pe->v = -(int64_t)pe->v;
    }
}

ST_FUNC void asm_expr(TCCState *S, ExprValue *pe)
{
    asm_expr_cmp(S, pe);
}

ST_FUNC int asm_int_expr(TCCState *S)
{
    ExprValue e;
    asm_expr(S, &e);
    if (e.sym)
        expect(S, "constant");
    return e.v;
}

static Sym* asm_new_label1(TCCState *S, int label, int is_local,
                           int sh_num, int value)
{
    Sym *sym;
    ElfSym *esym;

    sym = asm_label_find(S, label);
    if (sym) {
	esym = elfsym(S, sym);
	/* A VT_EXTERN symbol, even if it has a section is considered
	   overridable.  This is how we "define" .set targets.  Real
	   definitions won't have VT_EXTERN set.  */
        if (esym && esym->st_shndx != SHN_UNDEF) {
            /* the label is already defined */
            if (IS_ASM_SYM(sym)
                && (is_local == 1 || (sym->type.t & VT_EXTERN)))
                goto new_label;
            if (!(sym->type.t & VT_EXTERN))
                tcc_error(S, "assembler label '%s' already defined",
                          get_tok_str(S, label, NULL));
        }
    } else {
    new_label:
        sym = asm_label_push(S, label);
    }
    if (!sym->c)
      put_extern_sym2(S, sym, SHN_UNDEF, 0, 0, 1);
    esym = elfsym(S, sym);
    esym->st_shndx = sh_num;
    esym->st_value = value;
    if (is_local != 2)
        sym->type.t &= ~VT_EXTERN;
    return sym;
}

static Sym* asm_new_label(TCCState *S, int label, int is_local)
{
    return asm_new_label1(S, label, is_local, cur_text_section->sh_num, S->ind);
}

/* Set the value of LABEL to that of some expression (possibly
   involving other symbols).  LABEL can be overwritten later still.  */
static Sym* set_symbol(TCCState *S, int label)
{
    long n;
    ExprValue e;
    Sym *sym;
    ElfSym *esym;
    next(S);
    asm_expr(S, &e);
    n = e.v;
    esym = elfsym(S, e.sym);
    if (esym)
	n += esym->st_value;
    sym = asm_new_label1(S, label, 2, esym ? esym->st_shndx : SHN_ABS, n);
    elfsym(S, sym)->st_other |= ST_ASM_SET;
    return sym;
}

static void use_section1(TCCState *S, Section *sec)
{
    cur_text_section->data_offset = S->ind;
    cur_text_section = sec;
    S->ind = cur_text_section->data_offset;
}

static void use_section(TCCState *S, const char *name)
{
    Section *sec;
    sec = find_section(S, name);
    use_section1(S, sec);
}

static void push_section(TCCState *S, const char *name)
{
    Section *sec = find_section(S, name);
    sec->prev = cur_text_section;
    use_section1(S, sec);
}

static void pop_section(TCCState *S)
{
    Section *prev = cur_text_section->prev;
    if (!prev)
        tcc_error(S, ".popsection without .pushsection");
    cur_text_section->prev = NULL;
    use_section1(S, prev);
}

static void asm_parse_directive(TCCState *S, int global)
{
    int n, offset, v, size, tok1;
    Section *sec;
    uint8_t *ptr;

    /* assembler directive */
    sec = cur_text_section;
    switch(S->tok) {
    case TOK_ASMDIR_align:
    case TOK_ASMDIR_balign:
    case TOK_ASMDIR_p2align:
    case TOK_ASMDIR_skip:
    case TOK_ASMDIR_space:
        tok1 = S->tok;
        next(S);
        n = asm_int_expr(S);
        if (tok1 == TOK_ASMDIR_p2align)
        {
            if (n < 0 || n > 30)
                tcc_error(S, "invalid p2align, must be between 0 and 30");
            n = 1 << n;
            tok1 = TOK_ASMDIR_align;
        }
        if (tok1 == TOK_ASMDIR_align || tok1 == TOK_ASMDIR_balign) {
            if (n < 0 || (n & (n-1)) != 0)
                tcc_error(S, "alignment must be a positive power of two");
            offset = (S->ind + n - 1) & -n;
            size = offset - S->ind;
            /* the section must have a compatible alignment */
            if (sec->sh_addralign < n)
                sec->sh_addralign = n;
        } else {
	    if (n < 0)
	        n = 0;
            size = n;
        }
        v = 0;
        if (S->tok == ',') {
            next(S);
            v = asm_int_expr(S);
        }
    zero_pad:
        if (sec->sh_type != SHT_NOBITS) {
            sec->data_offset = S->ind;
            ptr = section_ptr_add(S, sec, size);
            memset(ptr, v, size);
        }
        S->ind += size;
        break;
    case TOK_ASMDIR_quad:
#ifdef TCC_TARGET_X86_64
	size = 8;
	goto asm_data;
#else
        next(S);
        for(;;) {
            uint64_t vl;
            const char *p;

            p = S->tokc.str.data;
            if (S->tok != TOK_PPNUM) {
            error_constant:
                tcc_error(S, "64 bit constant");
            }
            vl = strtoll(p, (char **)&p, 0);
            if (*p != '\0')
                goto error_constant;
            next(S);
            if (sec->sh_type != SHT_NOBITS) {
                /* XXX: endianness */
                gen_le32(S, vl);
                gen_le32(S, vl >> 32);
            } else {
                S->ind += 8;
            }
            if (S->tok != ',')
                break;
            next(S);
        }
        break;
#endif
    case TOK_ASMDIR_byte:
        size = 1;
        goto asm_data;
    case TOK_ASMDIR_word:
    case TOK_ASMDIR_short:
        size = 2;
        goto asm_data;
    case TOK_ASMDIR_long:
    case TOK_ASMDIR_int:
        size = 4;
    asm_data:
        next(S);
        for(;;) {
            ExprValue e;
            asm_expr(S, &e);
            if (sec->sh_type != SHT_NOBITS) {
                if (size == 4) {
                    gen_expr32(S, &e);
#ifdef TCC_TARGET_X86_64
		} else if (size == 8) {
		    gen_expr64(S, &e);
#endif
                } else {
                    if (e.sym)
                        expect(S, "constant");
                    if (size == 1)
                        g(S, e.v);
                    else
                        gen_le16(S, e.v);
                }
            } else {
                S->ind += size;
            }
            if (S->tok != ',')
                break;
            next(S);
        }
        break;
    case TOK_ASMDIR_fill:
        {
            int repeat, size, val, i, j;
            uint8_t repeat_buf[8];
            next(S);
            repeat = asm_int_expr(S);
            if (repeat < 0) {
                tcc_error(S, "repeat < 0; .fill ignored");
                break;
            }
            size = 1;
            val = 0;
            if (S->tok == ',') {
                next(S);
                size = asm_int_expr(S);
                if (size < 0) {
                    tcc_error(S, "size < 0; .fill ignored");
                    break;
                }
                if (size > 8)
                    size = 8;
                if (S->tok == ',') {
                    next(S);
                    val = asm_int_expr(S);
                }
            }
            /* XXX: endianness */
            repeat_buf[0] = val;
            repeat_buf[1] = val >> 8;
            repeat_buf[2] = val >> 16;
            repeat_buf[3] = val >> 24;
            repeat_buf[4] = 0;
            repeat_buf[5] = 0;
            repeat_buf[6] = 0;
            repeat_buf[7] = 0;
            for(i = 0; i < repeat; i++) {
                for(j = 0; j < size; j++) {
                    g(S, repeat_buf[j]);
                }
            }
        }
        break;
    case TOK_ASMDIR_rept:
        {
            int repeat;
            TokenString *init_str;
            next(S);
            repeat = asm_int_expr(S);
            init_str = tok_str_alloc(S);
            while (next(S), S->tok != TOK_ASMDIR_endr) {
                if (S->tok == CH_EOF)
                    tcc_error(S, "we at end of file, .endr not found");
                tok_str_add_tok(S, init_str);
            }
            tok_str_add(S, init_str, -1);
            tok_str_add(S, init_str, 0);
            begin_macro(S, init_str, 1);
            while (repeat-- > 0) {
                tcc_assemble_internal(S, (S->tccpp_parse_flags & PARSE_FLAG_PREPROCESS),
				      global);
                S->tccpp_macro_ptr = init_str->str;
            }
            end_macro(S);
            next(S);
            break;
        }
    case TOK_ASMDIR_org:
        {
            unsigned long n;
	    ExprValue e;
	    ElfSym *esym;
            next(S);
	    asm_expr(S, &e);
	    n = e.v;
	    esym = elfsym(S, e.sym);
	    if (esym) {
		if (esym->st_shndx != cur_text_section->sh_num)
		  expect(S, "constant or same-section symbol");
		n += esym->st_value;
	    }
            if (n < S->ind)
                tcc_error(S, "attempt to .org backwards");
            v = 0;
            size = n - S->ind;
            goto zero_pad;
        }
        break;
    case TOK_ASMDIR_set:
	next(S);
	tok1 = S->tok;
	next(S);
	/* Also accept '.set stuff', but don't do anything with this.
	   It's used in GAS to set various features like '.set mips16'.  */
	if (S->tok == ',')
	    set_symbol(S, tok1);
	break;
    case TOK_ASMDIR_globl:
    case TOK_ASMDIR_global:
    case TOK_ASMDIR_weak:
    case TOK_ASMDIR_hidden:
	tok1 = S->tok;
	do { 
            Sym *sym;
            next(S);
            sym = get_asm_sym(S, S->tok, NULL);
	    if (tok1 != TOK_ASMDIR_hidden)
                sym->type.t &= ~VT_STATIC;
            if (tok1 == TOK_ASMDIR_weak)
                sym->a.weak = 1;
	    else if (tok1 == TOK_ASMDIR_hidden)
	        sym->a.visibility = STV_HIDDEN;
            update_storage(S, sym);
            next(S);
	} while (S->tok == ',');
	break;
    case TOK_ASMDIR_string:
    case TOK_ASMDIR_ascii:
    case TOK_ASMDIR_asciz:
        {
            const uint8_t *p;
            int i, size, t;

            t = S->tok;
            next(S);
            for(;;) {
                if (S->tok != TOK_STR)
                    expect(S, "string constant");
                p = S->tokc.str.data;
                size = S->tokc.str.size;
                if (t == TOK_ASMDIR_ascii && size > 0)
                    size--;
                for(i = 0; i < size; i++)
                    g(S, p[i]);
                next(S);
                if (S->tok == ',') {
                    next(S);
                } else if (S->tok != TOK_STR) {
                    break;
                }
            }
	}
	break;
    case TOK_ASMDIR_text:
    case TOK_ASMDIR_data:
    case TOK_ASMDIR_bss:
	{ 
            char sname[64];
            tok1 = S->tok;
            n = 0;
            next(S);
            if (S->tok != ';' && S->tok != TOK_LINEFEED) {
		n = asm_int_expr(S);
		next(S);
            }
            if (n)
                sprintf(sname, "%s%d", get_tok_str(S, tok1, NULL), n);
            else
                sprintf(sname, "%s", get_tok_str(S, tok1, NULL));
            use_section(S, sname);
	}
	break;
    case TOK_ASMDIR_file:
        {
            char filename[512];

            filename[0] = '\0';
            next(S);
            if (S->tok == TOK_STR)
                pstrcat(filename, sizeof(filename), S->tokc.str.data);
            else
                pstrcat(filename, sizeof(filename), get_tok_str(S, S->tok, NULL));
            tcc_warning_c(warn_unsupported)(S, "ignoring .file %s", filename);
            next(S);
        }
        break;
    case TOK_ASMDIR_ident:
        {
            char ident[256];

            ident[0] = '\0';
            next(S);
            if (S->tok == TOK_STR)
                pstrcat(ident, sizeof(ident), S->tokc.str.data);
            else
                pstrcat(ident, sizeof(ident), get_tok_str(S, S->tok, NULL));
            tcc_warning_c(warn_unsupported)(S, "ignoring .ident %s", ident);
            next(S);
        }
        break;
    case TOK_ASMDIR_size:
        { 
            Sym *sym;

            next(S);
            sym = asm_label_find(S, S->tok);
            if (!sym) {
                tcc_error(S, "label not found: %s", get_tok_str(S, S->tok, NULL));
            }
            /* XXX .size name,label2-label1 */
            tcc_warning_c(warn_unsupported)(S, "ignoring .size %s,*", get_tok_str(S, S->tok, NULL));
            next(S);
            skip(S, ',');
            while (S->tok != TOK_LINEFEED && S->tok != ';' && S->tok != CH_EOF) {
                next(S);
            }
        }
        break;
    case TOK_ASMDIR_type:
        { 
            Sym *sym;
            const char *newtype;

            next(S);
            sym = get_asm_sym(S, S->tok, NULL);
            next(S);
            skip(S, ',');
            if (S->tok == TOK_STR) {
                newtype = S->tokc.str.data;
            } else {
                if (S->tok == '@' || S->tok == '%')
                    next(S);
                newtype = get_tok_str(S, S->tok, NULL);
            }

            if (!strcmp(newtype, "function") || !strcmp(newtype, "STT_FUNC")) {
                sym->type.t = (sym->type.t & ~VT_BTYPE) | VT_FUNC;
            } else
                tcc_warning_c(warn_unsupported)(S, "change type of '%s' from 0x%x to '%s' ignored",
                    get_tok_str(S, sym->v, NULL), sym->type.t, newtype);

            next(S);
        }
        break;
    case TOK_ASMDIR_pushsection:
    case TOK_ASMDIR_section:
        {
            char sname[256];
	    int old_nb_section = S->nb_sections;

	    tok1 = S->tok;
            /* XXX: support more options */
            next(S);
            sname[0] = '\0';
            while (S->tok != ';' && S->tok != TOK_LINEFEED && S->tok != ',') {
                if (S->tok == TOK_STR)
                    pstrcat(sname, sizeof(sname), S->tokc.str.data);
                else
                    pstrcat(sname, sizeof(sname), get_tok_str(S, S->tok, NULL));
                next(S);
            }
            if (S->tok == ',') {
                /* skip section options */
                next(S);
                if (S->tok != TOK_STR)
                    expect(S, "string constant");
                next(S);
                if (S->tok == ',') {
                    next(S);
                    if (S->tok == '@' || S->tok == '%')
                        next(S);
                    next(S);
                }
            }
            S->tccasm_last_text_section = cur_text_section;
	    if (tok1 == TOK_ASMDIR_section)
	        use_section(S, sname);
	    else
	        push_section(S, sname);
	    /* If we just allocated a new section reset its alignment to
	       1.  new_section normally acts for GCC compatibility and
	       sets alignment to PTR_SIZE.  The assembler behaves different. */
	    if (old_nb_section != S->nb_sections)
	        cur_text_section->sh_addralign = 1;
        }
        break;
    case TOK_ASMDIR_previous:
        { 
            Section *sec;
            next(S);
            if (!S->tccasm_last_text_section)
                tcc_error(S, "no previous section referenced");
            sec = cur_text_section;
            use_section1(S, S->tccasm_last_text_section);
            S->tccasm_last_text_section = sec;
        }
        break;
    case TOK_ASMDIR_popsection:
	next(S);
	pop_section(S);
	break;
#ifdef TCC_TARGET_I386
    case TOK_ASMDIR_code16:
        {
            next(S);
            S->seg_size = 16;
        }
        break;
    case TOK_ASMDIR_code32:
        {
            next(S);
            S->seg_size = 32;
        }
        break;
#endif
#ifdef TCC_TARGET_X86_64
    /* added for compatibility with GAS */
    case TOK_ASMDIR_code64:
        next(S);
        break;
#endif
    default:
        tcc_error(S, "unknown assembler directive '.%s'", get_tok_str(S, S->tok, NULL));
        break;
    }
}


/* assemble a file */
static int tcc_assemble_internal(TCCState *S, int do_preprocess, int global)
{
    int opcode;
    int saved_parse_flags = S->tccpp_parse_flags;

    S->tccpp_parse_flags = PARSE_FLAG_ASM_FILE | PARSE_FLAG_TOK_STR;
    if (do_preprocess)
        S->tccpp_parse_flags |= PARSE_FLAG_PREPROCESS;
    for(;;) {
        next(S);
        if (S->tok == TOK_EOF)
            break;
        S->tccpp_parse_flags |= PARSE_FLAG_LINEFEED; /* XXX: suppress that hack */
    redo:
        if (S->tok == '#') {
            /* horrible gas comment */
            while (S->tok != TOK_LINEFEED)
                next(S);
        } else if (S->tok >= TOK_ASMDIR_FIRST && S->tok <= TOK_ASMDIR_LAST) {
            asm_parse_directive(S, global);
        } else if (S->tok == TOK_PPNUM) {
            const char *p;
            int n;
            p = S->tokc.str.data;
            n = strtoul(p, (char **)&p, 10);
            if (*p != '\0')
                expect(S, "':'");
            /* new local label */
            asm_new_label(S, asm_get_local_label_name(S, n), 1);
            next(S);
            skip(S, ':');
            goto redo;
        } else if (S->tok >= TOK_IDENT) {
            /* instruction or label */
            opcode = S->tok;
            next(S);
            if (S->tok == ':') {
                /* new label */
                asm_new_label(S, opcode, 0);
                next(S);
                goto redo;
            } else if (S->tok == '=') {
		set_symbol(S, opcode);
                goto redo;
            } else {
                asm_opcode(S, opcode);
            }
        }
        /* end of line */
        if (S->tok != ';' && S->tok != TOK_LINEFEED)
            expect(S, "end of line");
        S->tccpp_parse_flags &= ~PARSE_FLAG_LINEFEED; /* XXX: suppress that hack */
    }

    S->tccpp_parse_flags = saved_parse_flags;
    return 0;
}

/* Assemble the current file */
ST_FUNC int tcc_assemble(TCCState *S, int do_preprocess)
{
    int ret;
    tcc_debug_start(S);
    /* default section is text */
    cur_text_section = text_section;
    S->ind = cur_text_section->data_offset;
    S->nocode_wanted = 0;
    ret = tcc_assemble_internal(S, do_preprocess, 1);
    cur_text_section->data_offset = S->ind;
    tcc_debug_end(S);
    return ret;
}

/********************************************************************/
/* GCC inline asm support */

/* assemble the string 'str' in the current C compilation unit without
   C preprocessing. NOTE: str is modified by modifying the '\0' at the
   end */
static void tcc_assemble_inline(TCCState *S, char *str, int len, int global)
{
    const int *saved_macro_ptr = S->tccpp_macro_ptr;
    int dotid = set_idnum(S, '.', IS_ID);
    int dolid = set_idnum(S, '$', 0);

    tcc_open_bf(S, ":asm:", len);
    memcpy(S->tccpp_file->buffer, str, len);
    S->tccpp_macro_ptr = NULL;
    tcc_assemble_internal(S, 0, global);
    tcc_close(S);

    set_idnum(S, '$', dolid);
    set_idnum(S, '.', dotid);
    S->tccpp_macro_ptr = saved_macro_ptr;
}

/* find a constraint by its number or id (gcc 3 extended
   syntax). return -1 if not found. Return in *pp in char after the
   constraint */
ST_FUNC int find_constraint(TCCState *S, ASMOperand *operands, int nb_operands, 
                           const char *name, const char **pp)
{
    int index;
    TokenSym *ts;
    const char *p;

    if (isnum(*name)) {
        index = 0;
        while (isnum(*name)) {
            index = (index * 10) + (*name) - '0';
            name++;
        }
        if ((unsigned)index >= nb_operands)
            index = -1;
    } else if (*name == '[') {
        name++;
        p = strchr(name, ']');
        if (p) {
            ts = tok_alloc(S, name, p - name);
            for(index = 0; index < nb_operands; index++) {
                if (operands[index].id == ts->tok)
                    goto found;
            }
            index = -1;
        found:
            name = p + 1;
        } else {
            index = -1;
        }
    } else {
        index = -1;
    }
    if (pp)
        *pp = name;
    return index;
}

static void subst_asm_operands(TCCState *S, ASMOperand *operands, int nb_operands, 
                               CString *out_str, CString *in_str)
{
    int c, index, modifier;
    const char *str;
    ASMOperand *op;
    SValue sv;

    cstr_new(S, out_str);
    str = in_str->data;
    for(;;) {
        c = *str++;
        if (c == '%') {
            if (*str == '%') {
                str++;
                goto add_char;
            }
            modifier = 0;
            if (*str == 'c' || *str == 'n' ||
                *str == 'b' || *str == 'w' || *str == 'h' || *str == 'k' ||
		*str == 'q' ||
		/* P in GCC would add "@PLT" to symbol refs in PIC mode,
		   and make literal operands not be decorated with '$'.  */
		*str == 'P')
                modifier = *str++;
            index = find_constraint(S, operands, nb_operands, str, &str);
            if (index < 0)
                tcc_error(S, "invalid operand reference after %%");
            op = &operands[index];
            sv = *op->vt;
            if (op->reg >= 0) {
                sv.r = op->reg;
                if ((op->vt->r & VT_VALMASK) == VT_LLOCAL && op->is_memory)
                    sv.r |= VT_LVAL;
            }
            subst_asm_operand(S, out_str, &sv, modifier);
        } else {
        add_char:
            cstr_ccat(S, out_str, c);
            if (c == '\0')
                break;
        }
    }
}


static void parse_asm_operands(TCCState *S, ASMOperand *operands, int *nb_operands_ptr,
                               int is_output)
{
    ASMOperand *op;
    int nb_operands;

    if (S->tok != ':') {
        nb_operands = *nb_operands_ptr;
        for(;;) {
	    CString astr;
            if (nb_operands >= MAX_ASM_OPERANDS)
                tcc_error(S, "too many asm operands");
            op = &operands[nb_operands++];
            op->id = 0;
            if (S->tok == '[') {
                next(S);
                if (S->tok < TOK_IDENT)
                    expect(S, "identifier");
                op->id = S->tok;
                next(S);
                skip(S, ']');
            }
	    parse_mult_str(S, &astr, "string constant");
            op->constraint = tcc_malloc(S, astr.size);
            strcpy(op->constraint, astr.data);
	    cstr_free(S, &astr);
            skip(S, '(');
            gexpr(S);
            if (is_output) {
                if (!(S->vtop->type.t & VT_ARRAY))
                    test_lvalue(S);
            } else {
                /* we want to avoid LLOCAL case, except when the 'm'
                   constraint is used. Note that it may come from
                   register storage, so we need to convert (reg)
                   case */
                if ((S->vtop->r & VT_LVAL) &&
                    ((S->vtop->r & VT_VALMASK) == VT_LLOCAL ||
                     (S->vtop->r & VT_VALMASK) < VT_CONST) &&
                    !strchr(op->constraint, 'm')) {
                    gv(S, RC_INT);
                }
            }
            op->vt = S->vtop;
            skip(S, ')');
            if (S->tok == ',') {
                next(S);
            } else {
                break;
            }
        }
        *nb_operands_ptr = nb_operands;
    }
}

/* parse the GCC asm() instruction */
ST_FUNC void asm_instr(TCCState *S)
{
    CString astr, astr1;
    ASMOperand operands[MAX_ASM_OPERANDS];
    int nb_outputs, nb_operands, i, must_subst, out_reg;
    uint8_t clobber_regs[NB_ASM_REGS];
    Section *sec;

    /* since we always generate the asm() instruction, we can ignore
       volatile */
    if (S->tok == TOK_VOLATILE1 || S->tok == TOK_VOLATILE2 || S->tok == TOK_VOLATILE3) {
        next(S);
    }
    parse_asm_str(S, &astr);
    nb_operands = 0;
    nb_outputs = 0;
    must_subst = 0;
    memset(clobber_regs, 0, sizeof(clobber_regs));
    if (S->tok == ':') {
        next(S);
        must_subst = 1;
        /* output args */
        parse_asm_operands(S, operands, &nb_operands, 1);
        nb_outputs = nb_operands;
        if (S->tok == ':') {
            next(S);
            if (S->tok != ')') {
                /* input args */
                parse_asm_operands(S, operands, &nb_operands, 0);
                if (S->tok == ':') {
                    /* clobber list */
                    /* XXX: handle registers */
                    next(S);
                    for(;;) {
                        if (S->tok != TOK_STR)
                            expect(S, "string constant");
                        asm_clobber(S, clobber_regs, S->tokc.str.data);
                        next(S);
                        if (S->tok == ',') {
                            next(S);
                        } else {
                            break;
                        }
                    }
                }
            }
        }
    }
    skip(S, ')');
    /* NOTE: we do not eat the ';' so that we can restore the current
       token after the assembler parsing */
    if (S->tok != ';')
        expect(S, "';'");
    
    /* save all values in the memory */
    save_regs(S, 0);

    /* compute constraints */
    asm_compute_constraints(S, operands, nb_operands, nb_outputs, 
                            clobber_regs, &out_reg);

    /* substitute the operands in the asm string. No substitution is
       done if no operands (GCC behaviour) */
#ifdef ASM_DEBUG
    printf("asm: \"%s\"\n", (char *)astr.data);
#endif
    if (must_subst) {
        subst_asm_operands(S, operands, nb_operands, &astr1, &astr);
        cstr_free(S, &astr);
    } else {
        astr1 = astr;
    }
#ifdef ASM_DEBUG
    printf("subst_asm: \"%s\"\n", (char *)astr1.data);
#endif

    /* generate loads */
    asm_gen_code(S, operands, nb_operands, nb_outputs, 0, 
                 clobber_regs, out_reg);    

    /* We don't allow switching section within inline asm to
       bleed out to surrounding code.  */
    sec = cur_text_section;
    /* assemble the string with tcc internal assembler */
    tcc_assemble_inline(S, astr1.data, astr1.size - 1, 0);
    if (sec != cur_text_section) {
        tcc_warning(S, "inline asm tries to change current section");
        use_section1(S, sec);
    }

    /* restore the current C token */
    next(S);

    /* store the output values if needed */
    asm_gen_code(S, operands, nb_operands, nb_outputs, 1, 
                 clobber_regs, out_reg);
    
    /* free everything */
    for(i=0;i<nb_operands;i++) {
        ASMOperand *op;
        op = &operands[i];
        tcc_free(S, op->constraint);
        vpop(S);
    }
    cstr_free(S, &astr1);
}

ST_FUNC void asm_global_instr(TCCState *S)
{
    CString astr;
    int saved_nocode_wanted = S->nocode_wanted;

    /* Global asm blocks are always emitted.  */
    S->nocode_wanted = 0;
    next(S);
    parse_asm_str(S, &astr);
    skip(S, ')');
    /* NOTE: we do not eat the ';' so that we can restore the current
       token after the assembler parsing */
    if (S->tok != ';')
        expect(S, "';'");
    
#ifdef ASM_DEBUG
    printf("asm_global: \"%s\"\n", (char *)astr.data);
#endif
    cur_text_section = text_section;
    S->ind = cur_text_section->data_offset;

    /* assemble the string with tcc internal assembler */
    tcc_assemble_inline(S, astr.data, astr.size - 1, 1);
    
    cur_text_section->data_offset = S->ind;

    /* restore the current C token */
    next(S);

    cstr_free(S, &astr);
    S->nocode_wanted = saved_nocode_wanted;
}

/********************************************************/
#else
ST_FUNC int tcc_assemble(TCCState *S, int do_preprocess)
{
    tcc_error(S, "asm not supported");
}

ST_FUNC void asm_instr(TCCState *S)
{
    tcc_error(S, "inline asm() not supported");
}

ST_FUNC void asm_global_instr(TCCState *S)
{
    tcc_error(S, "inline asm() not supported");
}
#endif /* CONFIG_TCC_ASM */
