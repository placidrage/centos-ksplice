/*  Copyright (C) 2007-2008  Jeffrey Brian Arnold <jbarnold@mit.edu>
 *  Copyright (C) 2008  Anders Kaseorg <andersk@mit.edu>,
 *                      Tim Abbott <tabbott@mit.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#ifndef FUNCTION_SECTIONS
#include "udis86.h"

/* Various efficient no-op patterns for aligning code labels.
   Note: Don't try to assemble the instructions in the comments.
   0L and 0w are not legal. */

#define NUM_NOPS (sizeof(nops) / sizeof(nops[0]))
struct insn {
	size_t len;
	const unsigned char *data;
};

/* *INDENT-OFF* */
#define I(...) {							\
		.len = sizeof((const unsigned char []){__VA_ARGS__}),	\
		.data = ((const unsigned char []){__VA_ARGS__}),	\
	}
static const struct insn nops[] = {
/* GNU assembler no-op patterns from
   binutils-2.17/gas/config/tc-i386.c line 500 */
I(0x90),					/* nop                  */
I(0x89, 0xf6),					/* movl %esi,%esi       */
I(0x8d, 0x76, 0x00),				/* leal 0(%esi),%esi    */
I(0x8d, 0x74, 0x26, 0x00),			/* leal 0(%esi,1),%esi  */
I(0x90,						/* nop                  */
  0x8d, 0x74, 0x26, 0x00),			/* leal 0(%esi,1),%esi  */
I(0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00),		/* leal 0L(%esi),%esi   */
I(0x8d, 0xb4, 0x26, 0x00, 0x00, 0x00, 0x00),	/* leal 0L(%esi,1),%esi */
I(0x90,						/* nop                  */
  0x8d, 0xb4, 0x26, 0x00, 0x00, 0x00, 0x00),	/* leal 0L(%esi,1),%esi */
I(0x89, 0xf6,					/* movl %esi,%esi       */
  0x8d, 0xbc, 0x27, 0x00, 0x00, 0x00, 0x00),	/* leal 0L(%edi,1),%edi */
I(0x8d, 0x76, 0x00,				/* leal 0(%esi),%esi    */
  0x8d, 0xbc, 0x27, 0x00, 0x00, 0x00, 0x00),	/* leal 0L(%edi,1),%edi */
I(0x8d, 0x74, 0x26, 0x00,			/* leal 0(%esi,1),%esi  */
  0x8d, 0xbc, 0x27, 0x00, 0x00, 0x00, 0x00),	/* leal 0L(%edi,1),%edi */
I(0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00,		/* leal 0L(%esi),%esi   */
  0x8d, 0xbf, 0x00, 0x00, 0x00, 0x00),		/* leal 0L(%edi),%edi   */
I(0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00,		/* leal 0L(%esi),%esi   */
  0x8d, 0xbc, 0x27, 0x00, 0x00, 0x00, 0x00),	/* leal 0L(%edi,1),%edi */
I(0x8d, 0xb4, 0x26, 0x00, 0x00, 0x00, 0x00,	/* leal 0L(%esi,1),%esi */
  0x8d, 0xbc, 0x27, 0x00, 0x00, 0x00, 0x00),	/* leal 0L(%edi,1),%edi */
I(0xeb, 0x0d, 0x90, 0x90, 0x90, 0x90, 0x90,	/* jmp .+15; lotsa nops */
  0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90),

/* binutils-2.17/gas/config/tc-i386.c line 570 */
I(0x66, 0x90),					/* xchg %ax,%ax         */
I(0x66,						/* data16               */
  0x66, 0x90),					/* xchg %ax,%ax         */
I(0x66,						/* data16               */
  0x66,						/* data16               */
  0x66, 0x90),					/* xchg %ax,%ax         */
I(0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66, 0x90),					/* xchg %ax,%ax         */

/* binutils-2.18/gas/config/tc-i386.c line 572 */
I(0x0f, 0x1f, 0x00),				/* nopl (%[re]ax)       */
I(0x0f, 0x1f, 0x40, 0x00),			/* nopl 0(%[re]ax)      */
I(0x0f, 0x1f, 0x44, 0x00, 0x00),	/* nopl 0(%[re]ax,%[re]ax,1)    */
I(0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00),	/* nopw 0(%[re]ax,%[re]ax,1)    */
I(0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00),
					/* nopw 0(%[re]ax,%[re]ax,1)    */
I(0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
					/* nopl 0L(%[re]ax,%[re]ax,1)   */
I(0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
					/* nopw 0L(%[re]ax,%[re]ax,1)   */
I(0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
				/* nopw %cs:0L(%[re]ax,%[re]ax,1)       */
I(0x66,						/* data16               */
  0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
				/* nopw %cs:0L(%[re]ax,%[re]ax,1)       */
I(0x66,						/* data16               */
  0x66,						/* data16               */
  0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
				/* nopw %cs:0L(%[re]ax,%[re]ax,1)       */
I(0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
				/* nopw %cs:0L(%[re]ax,%[re]ax,1)       */
I(0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
				/* nopw %cs:0L(%[re]ax,%[re]ax,1)       */
I(0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
				/* nopw %cs:0L(%[re]ax,%[re]ax,1)       */
I(0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66,						/* data16               */
  0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
				/* nopw %cs:0L(%[re]ax,%[re]ax,1)       */
I(0x0f, 0x1f, 0x44, 0x00, 0x00,		/* nopl 0(%[re]ax,%[re]ax,1)    */
  0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00),	/* nopw 0(%[re]ax,%[re]ax,1)    */
I(0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00,	/* nopw 0(%[re]ax,%[re]ax,1)    */
  0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00),	/* nopw 0(%[re]ax,%[re]ax,1)    */
I(0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00,	/* nopw 0(%[re]ax,%[re]ax,1)    */
  0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00),	/* nopl 0L(%[re]ax)     */
I(0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00,	/* nopl 0L(%[re]ax)     */
  0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00),	/* nopl 0L(%[re]ax)     */
I(0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00,	/* nopl 0L(%[re]ax)     */
  0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00),
					/* nopl 0L(%[re]ax,%[re]ax,1)   */
};
/* *INDENT-ON* */

static abort_t compare_operands(struct module_pack *pack,
				const struct ksplice_size *s,
				unsigned long run_addr,
				const unsigned char *run,
				const unsigned char *pre, struct ud *run_ud,
				struct ud *pre_ud, int opnum, int rerun);
static int match_nop(const unsigned char *addr);
static uint8_t ud_operand_len(struct ud_operand *operand);
static uint8_t ud_prefix_len(struct ud *ud);
static long jump_lval(struct ud_operand *operand);
static int next_run_byte(struct ud *ud);

static abort_t run_pre_cmp(struct module_pack *pack,
			   const struct ksplice_size *s,
			   unsigned long run_addr, int rerun)
{
	int runc, prec;
	int i;
	abort_t ret;
	const unsigned char *run, *pre;
	struct ud pre_ud, run_ud;
	unsigned long pre_addr = s->thismod_addr;

	if (s->size == 0)
		return NO_MATCH;

	run_addr = follow_trampolines(pack, run_addr);

	run = (const unsigned char *)run_addr;
	pre = (const unsigned char *)pre_addr;

	ud_init(&pre_ud);
	ud_set_mode(&pre_ud, BITS_PER_LONG);
	ud_set_syntax(&pre_ud, UD_SYN_ATT);
	ud_set_input_buffer(&pre_ud, (unsigned char *)pre, s->size);
	ud_set_pc(&pre_ud, 0);

	ud_init(&run_ud);
	ud_set_mode(&run_ud, BITS_PER_LONG);
	ud_set_syntax(&run_ud, UD_SYN_ATT);
	ud_set_input_hook(&run_ud, next_run_byte);
	ud_set_pc(&run_ud, 0);
	run_ud.userdata = (unsigned char *)run_addr;

	while (1) {
		/* Nops are the only sense in which the instruction
		   sequences are allowed to not match */
		runc = match_nop(run);
		prec = match_nop(pre);
		if (runc > 0 || prec > 0) {
			if (rerun)
				print_bytes(pack, run, runc, pre, prec);
			ud_input_skip(&run_ud, runc);
			ud_input_skip(&pre_ud, prec);
			run += runc;
			pre += prec;
			continue;
		}
		if (ud_disassemble(&pre_ud) == 0) {
			/* Ran out of pre bytes to match; we're done! */
			const struct ksplice_patch *p;
			int bytes_matched = (unsigned long)run - run_addr;
			if (bytes_matched >= 5)
				return OK;
			for (p = pack->patches; p < pack->patches_end; p++) {
				if (p->oldaddr == run_addr) {
					print_abort(pack, "Function too short "
						    "for trampoline");
					return NO_MATCH;
				}
			}
			return OK;
		}
		if (ud_disassemble(&run_ud) == 0)
			return NO_MATCH;

		if (rerun)
			ksdebug(pack, 0, "| ");
		if (rerun)
			print_bytes(pack, run, ud_insn_len(&run_ud),
				    pre, ud_insn_len(&pre_ud));

		if (run_ud.mnemonic != pre_ud.mnemonic) {
			if (rerun)
				ksdebug(pack, 3, "mnemonic mismatch: %s %s\n",
					ud_lookup_mnemonic(run_ud.mnemonic),
					ud_lookup_mnemonic(pre_ud.mnemonic));
			return NO_MATCH;
		}
		if (s->extended_size == 0 && run_ud.mnemonic == UD_Ijmp) {
			ksdebug(pack, 3, KERN_DEBUG "Matched %lx bytes of locks"
				" section\n", (unsigned long)run - run_addr);
			return OK;
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20) && \
    defined(_I386_BUG_H) && (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11) || \
			     defined(CONFIG_DEBUG_BUGVERBOSE))
/* 91768d6c2bad0d2766a166f13f2f57e197de3458 was after 2.6.19 */
/* 38326f786cf4529a86b1ccde3aa17f4fa7e8472a was after 2.6.10 */
		if (run_ud.mnemonic == UD_Iud2) {
			/* ud2 means BUG().  On old i386 kernels, it is followed
			   by 2 bytes and then a 4-byte relocation; and is not
			   disassembler-friendly. */
			int matched = 0;
			ret = handle_reloc(pack, (unsigned long)(pre + 4),
					   (unsigned long)(run + 4), rerun,
					   &matched);
			if (ret != OK)
				return ret;
			if (matched > 0) {
				/* If there's a relocation, then it's a BUG? */
				if (rerun) {
					ksdebug(pack, 0, "[BUG?: ");
					print_bytes(pack, run + 2, 6, pre + 2,
						    6);
					ksdebug(pack, 0, "] ");
				}
				pre += 8;
				run += 8;
				ud_input_skip(&run_ud, 6);
				ud_input_skip(&pre_ud, 6);
				continue;
			} else {
				if (!rerun)
					ksdebug(pack, 0, KERN_DEBUG
						"Unrecognized ud2\n");
				return NO_MATCH;
			}
		}
#endif /* LINUX_VERSION_CODE && _I386_BUG_H && CONFIG_DEBUG_BUGVERBOSE */

		for (i = 0; i < ARRAY_SIZE(run_ud.operand); i++) {
			ret = compare_operands(pack, s, run_addr, run, pre,
					       &run_ud, &pre_ud, i, rerun);
			if (ret != OK)
				return ret;
		}

		run += ud_insn_len(&run_ud);
		pre += ud_insn_len(&pre_ud);
	}
}

static abort_t compare_operands(struct module_pack *pack,
				const struct ksplice_size *s,
				unsigned long run_addr,
				const unsigned char *run,
				const unsigned char *pre, struct ud *run_ud,
				struct ud *pre_ud, int opnum, int rerun)
{
	abort_t ret;
	int i, matched = 0;
	unsigned long pre_addr = s->thismod_addr;
	struct ud_operand *run_op = &run_ud->operand[opnum];
	struct ud_operand *pre_op = &pre_ud->operand[opnum];
	uint8_t run_off = ud_prefix_len(run_ud);
	uint8_t pre_off = ud_prefix_len(pre_ud);
	for (i = 0; i < opnum; i++) {
		run_off += ud_operand_len(&run_ud->operand[i]);
		pre_off += ud_operand_len(&pre_ud->operand[i]);
	}

	if (run_op->type != pre_op->type) {
		if (rerun)
			ksdebug(pack, 3, "type mismatch: %d %d\n", run_op->type,
				pre_op->type);
		return NO_MATCH;
	}
	if (run_op->base != pre_op->base) {
		if (rerun)
			ksdebug(pack, 3, "base mismatch: %d %d\n", run_op->base,
				pre_op->base);
		return NO_MATCH;
	}
	if (run_op->index != pre_op->index) {
		if (rerun)
			ksdebug(pack, 3, "index mismatch: %d %d\n",
				run_op->index, pre_op->index);
		return NO_MATCH;
	}
	if (ud_operand_len(run_op) == 0 && ud_operand_len(pre_op) == 0)
		return OK;

	ret = handle_reloc(pack, (unsigned long)(pre + pre_off),
				(unsigned long)(run + run_off),
				rerun, &matched);
	if (ret != OK) {
		if (rerun)
			ksdebug(pack, 3, KERN_DEBUG "Matching failure at "
				"offset %lx\n", (unsigned long)pre - pre_addr);
		return ret;
	}
	if (matched != 0)
		/* This operand is a successfully processed relocation */
		return OK;
	if (pre_op->type == UD_OP_JIMM) {
		/* Immediate jump without a relocation */
		unsigned long pre_target = (unsigned long)pre +
		    ud_insn_len(pre_ud) + jump_lval(pre_op);
		unsigned long run_target = (unsigned long)run +
		    ud_insn_len(run_ud) + jump_lval(run_op);
		if (pre_target >= pre_addr + s->size &&
		    pre_target < pre_addr + s->extended_size) {
			struct ksplice_size smplocks_size = {
				.symbol = NULL,
				.size = 1000000,
				.extended_size = 0,
				.flags = KSPLICE_SIZE_TEXT,
				.thismod_addr = pre_target,
			};
			if (rerun)
				ksdebug(pack, 3, "Locks section %lx %lx: ",
					run_target, pre_target);
			if (rerun)
				ksdebug(pack, 3, "[ ");
			/* jump into .text.lock subsection */
			ret = run_pre_cmp(pack, &smplocks_size, run_target,
					  rerun);
			if (rerun)
				ksdebug(pack, 3, "] ");
			if (ret != OK) {
				if (!rerun)
					ksdebug(pack, 3, KERN_DEBUG
						"Locks section mismatch: %lx "
						"%lx\n", run_target,
						pre_target);
				return ret;
			}
			return OK;
		} else if (s->extended_size == 0) {
			/* FIXME: Ignoring targets of jumps out of smplocks */
			return OK;
		} else if (pre_target == run_target) {
			/* Paravirt-inserted pcrel jump; OK! */
			return OK;
		} else if (pre_target >= pre_addr &&
			   pre_target < pre_addr + s->size) {
			/* Jump within the current function.
			   We should ideally check it's to a corresponding place */
			return OK;
		} else {
			if (rerun) {
				ksdebug(pack, 3, "<--Different operands!\n");
				ksdebug(pack, 3, KERN_DEBUG
					"%lx %lx %lx %lx %x %lx %lx %lx\n",
					pre_addr, pre_target,
					pre_addr + s->size, (unsigned long)pre,
					ud_insn_len(pre_ud), s->size,
					jump_lval(pre_op), run_target);
			}
			return NO_MATCH;
		}
	} else if (ud_operand_len(pre_op) == ud_operand_len(run_op) &&
		   memcmp(pre + pre_off, run + run_off,
			  ud_operand_len(run_op)) == 0) {
		return OK;
	} else {
		if (rerun)
			ksdebug(pack, 3, "<--Different operands!\n");
		return NO_MATCH;
	}
}

static int match_nop(const unsigned char *addr)
{
	int i, j;
	const struct insn *nop;
	for (i = NUM_NOPS - 1; i >= 0; i--) {
		nop = &nops[i];
		for (j = 0; j < nop->len; j++) {
			if (!virtual_address_mapped((unsigned long)&addr[j]))
				break;
			if (addr[j] != nop->data[j])
				break;
		}
		if (j == nop->len)
			return j;
	}
	return 0;
}

static uint8_t ud_operand_len(struct ud_operand *operand)
{
	if (operand->type == UD_OP_MEM)
		return operand->offset / 8;
	if (operand->type == UD_OP_REG)
		return 0;
	return operand->size / 8;
}

static uint8_t ud_prefix_len(struct ud *ud)
{
	int len = ud_insn_len(ud);
	int i;
	for (i = 0; i < ARRAY_SIZE(ud->operand); i++)
		len -= ud_operand_len(&ud->operand[i]);
	return len;
}

static long jump_lval(struct ud_operand *operand)
{
	if (operand->type == UD_OP_JIMM) {
		switch(operand->size) {
		case 8:
			return operand->lval.sbyte;
		case 16:
			return operand->lval.sword;
		case 32:
			return operand->lval.sdword;
		case 64:
			return operand->lval.sqword;
		default:
			return 0;
		}
	}
	return 0;
}

static int next_run_byte(struct ud *ud)
{
	unsigned char byte;
	unsigned char *run_ptr = ud->userdata;
	if (!virtual_address_mapped((unsigned long)run_ptr))
		return UD_EOI;
	byte = *run_ptr;
	run_ptr++;
	ud->userdata = run_ptr;
	return byte;
}
#endif /* !FUNCTION_SECTIONS */

static unsigned long follow_trampolines(struct module_pack *pack,
					unsigned long addr)
{
	if (virtual_address_mapped(addr) &&
	    virtual_address_mapped(addr + 5 - 1) &&
	    *((const unsigned char *)addr) == 0xE9) {
		/* Remember to add the length of the e9 */
		unsigned long new_addr = addr + 5 + *(int32_t *)(addr + 1);
		/* Confirm that it is a jump into a ksplice module */
		struct module *m = __module_text_address(new_addr);
		if (m != NULL && m != pack->target &&
		    strncmp(m->name, "ksplice", strlen("ksplice")) == 0) {
			ksdebug(pack, 3, KERN_DEBUG "ksplice: Following "
				"trampoline %lx %lx\n", addr, new_addr);
			addr = new_addr;
		}
	}
	return addr;
}

static abort_t create_trampoline(struct ksplice_patch *p)
{
	p->trampoline[0] = 0xE9;
	*(u32 *)(&p->trampoline[1]) = p->repladdr - (p->oldaddr + 5);
	p->size = 5;
	return OK;
}

static abort_t handle_paravirt(struct module_pack *pack, unsigned long pre_addr,
			       unsigned long run_addr, int *matched)
{
	int32_t *run = (int32_t *)(run_addr + 1);
	int32_t *pre = (int32_t *)(pre_addr + 1);
	*matched = 0;

	if (!virtual_address_mapped(run_addr + 5) ||
	    !virtual_address_mapped(pre_addr + 5))
		return OK;

	if ((*(uint8_t *)run_addr == 0xe8 && *(uint8_t *)pre_addr == 0xe8) ||
	    (*(uint8_t *)run_addr == 0xe9 && *(uint8_t *)pre_addr == 0xe9))
		if ((unsigned long)run + *run == (unsigned long)pre + *pre)
			*matched = 5;
	return OK;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
static int virtual_address_mapped(unsigned long addr)
{
	unsigned int level;
	pte_t *pte = lookup_address(addr, &level);
	return pte == NULL ? 0 : pte_present(*pte);
}
#else /* LINUX_VERSION_CODE < */
/* f0646e43acb18f0e00b00085dc88bc3f403e7930 was after 2.6.24 */
static int virtual_address_mapped(unsigned long addr)
{
	pgd_t *pgd = pgd_offset_k(addr);
#ifdef pud_page
	pud_t *pud;
#endif /* pud_page */
	pmd_t *pmd;
	pte_t *pte;

	if (!pgd_present(*pgd))
		return 0;

#ifdef pud_page
	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		return 0;

	pmd = pmd_offset(pud, addr);
#else /* pud_page */
	pmd = pmd_offset(pgd, addr);
#endif /* pud_page */

	if (!pmd_present(*pmd))
		return 0;

	if (pmd_large(*pmd))
		return 1;

	pte = pte_offset_kernel(pmd, addr);
	if (!pte_present(*pte))
		return 0;

	return 1;
}
#endif /* LINUX_VERSION_CODE */
