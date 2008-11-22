/*  Copyright (C) 2007-2008  Jeff Arnold <jbarnold@mit.edu>
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

#if defined(_ASM_X86_PROCESSOR_H) || \
    defined(__ASM_X86_PROCESSOR_H)	/* New unified x86 */
#define KSPLICE_IP(x) ((x)->thread.ip)
#define KSPLICE_SP(x) ((x)->thread.sp)
#elif defined(CONFIG_X86_64)	/* Old x86 64-bit */
/* The IP is on the stack, so we don't need to check it separately.
 * Instead, we need to prevent Ksplice from patching thread_return.
 */
extern const char thread_return[];
#define KSPLICE_IP(x) ((unsigned long)thread_return)
#define KSPLICE_SP(x) ((x)->thread.rsp)
#else /* Old x86 32-bit */
#define KSPLICE_IP(x) ((x)->thread.eip)
#define KSPLICE_SP(x) ((x)->thread.esp)
#endif /* __ASM_X86_PROCESSOR_H */

#ifndef CONFIG_FUNCTION_DATA_SECTIONS
#include "udis86.h"
#ifdef CONFIG_FTRACE
#include <asm/ftrace.h>
#include <linux/ftrace.h>

extern ftrace_func_t ftrace_trace_function;
#endif /* CONFIG_FTRACE */

#define N_BITS(n) ((n) < sizeof(long) * 8 ? ~(~0L << (n)) : ~0L)

static abort_t compare_instructions(struct ksplice_pack *pack,
				    struct ksplice_section *sect,
				    const struct ksplice_reloc **fingerp,
				    const unsigned char *run_start,
				    const unsigned char *run,
				    const unsigned char *pre, struct ud *run_ud,
				    struct ud *pre_ud, enum run_pre_mode mode);
static abort_t compare_operands(struct ksplice_pack *pack,
				struct ksplice_section *sect,
				const struct ksplice_reloc **fingerp,
				const unsigned char *run_start,
				const unsigned char *run,
				const unsigned char *pre, struct ud *run_ud,
				struct ud *pre_ud, int opnum,
				enum run_pre_mode mode);
static uint8_t ud_operand_len(struct ud_operand *operand);
static uint8_t ud_prefix_len(struct ud *ud);
static long ud_operand_lval(struct ud_operand *operand);
static int next_run_byte(struct ud *ud);
static bool is_nop(struct ud *ud);
static bool is_unconditional_jump(struct ud *ud);
static bool is_mcount_call(struct ud *ud, unsigned long addr);
static void initialize_ksplice_ud(struct ud *ud);

static abort_t arch_run_pre_cmp(struct ksplice_pack *pack,
				struct ksplice_section *sect,
				unsigned long run_addr,
				struct list_head *safety_records,
				enum run_pre_mode mode)
{
	abort_t ret;
	const unsigned char *run, *pre, *run_start, *pre_start, *safety_start;
	struct ud pre_ud, run_ud;
	const unsigned char **match_map;
	const struct ksplice_reloc *finger;
	unsigned long pre_offset, run_offset;
	bool run_unconditional = false;
	bool pre_nop = true, run_nop = true;

	if (sect->size == 0)
		return NO_MATCH;

	pre_start = (const unsigned char *)sect->address;
	run_start = (const unsigned char *)run_addr;

	finger = init_reloc_search(pack, sect);

	run = run_start;
	pre = pre_start;

	initialize_ksplice_ud(&pre_ud);
	ud_set_input_buffer(&pre_ud, (unsigned char *)pre, sect->size);

	initialize_ksplice_ud(&run_ud);
	ud_set_input_hook(&run_ud, next_run_byte);
	ud_set_user_opaque_data(&run_ud, (unsigned char *)run_addr);
	safety_start = run_start;

	match_map = vmalloc(sizeof(*match_map) * sect->size);
	if (match_map == NULL)
		return OUT_OF_MEMORY;
	memset(match_map, 0, sizeof(*match_map) * sect->size);
	match_map[0] = run_start;
	sect->match_map = match_map;

	while (1) {
		if (pre_nop && ud_disassemble(&pre_ud) == 0) {
			/* Ran out of pre bytes to match; we're done! */
			unsigned long safety_offset = run - safety_start;
			ret = create_safety_record(pack, sect, safety_records,
						   (unsigned long)safety_start,
						   safety_offset);
			goto out;
		}
		if (run_nop && ud_disassemble(&run_ud) == 0) {
			ret = NO_MATCH;
			goto out;
		}
		pre_nop = is_nop(&pre_ud) ||
		    is_mcount_call(&pre_ud, (unsigned long)pre);
		run_nop = is_nop(&run_ud) ||
		    is_mcount_call(&run_ud, (unsigned long)run);
		if (pre_nop && !run_nop) {
			if (mode == RUN_PRE_DEBUG) {
				ksdebug(pack, "| nop: ");
				print_bytes(pack, run, 0, pre,
					    ud_insn_len(&pre_ud));
			}
			pre += ud_insn_len(&pre_ud);
			continue;
		}
		if (run_nop && !pre_nop) {
			if (mode == RUN_PRE_DEBUG) {
				ksdebug(pack, "| nop: ");
				print_bytes(pack, run, ud_insn_len(&run_ud),
					    pre, 0);
			}
			run += ud_insn_len(&run_ud);
			continue;
		}
		if (run_nop && pre_nop) {
			ret = compare_instructions(pack, sect, &finger,
						   run_start, run, pre, &run_ud,
						   &pre_ud, RUN_PRE_SILENT);
			if (ret != OK) {
				if (mode == RUN_PRE_DEBUG) {
					ksdebug(pack, "| nop: ");
					print_bytes(pack, run,
						    ud_insn_len(&run_ud), pre,
						    ud_insn_len(&pre_ud));
				}
				run += ud_insn_len(&run_ud);
				pre += ud_insn_len(&pre_ud);
				continue;
			} else if (ret != NO_MATCH && ret != OK) {
				goto out;
			}
		}
		pre_offset = pre - pre_start;

		if (match_map[pre_offset] == NULL) {
			match_map[pre_offset] = run;
		} else if (match_map[pre_offset] != run) {
			/* There is a discontinuity in the match map.
			   Check that the last instruction was an
			   unconditional change of control */
			if (!run_unconditional) {
				ksdebug(pack, "<--[No unconditional change of "
					"control at control transfer point %lx]"
					"\n", pre_offset);
				ret = NO_MATCH;
				goto out;
			}

			if (mode == RUN_PRE_DEBUG)
				ksdebug(pack, " [Moving run pointer for %lx "
					"from %lx to %lx]\n", pre_offset,
					(unsigned long)(run - run_start),
					(unsigned long)(match_map[pre_offset] -
							run_start));

			/* Create a safety_record for the block just matched */
			ret = create_safety_record(pack, sect, safety_records,
						   (unsigned long)safety_start,
						   run - safety_start);
			if (ret != OK)
				goto out;

			/* We re-initialize the run ud structure because
			   it may have cached upcoming bytes */
			run = match_map[pre_offset];
			initialize_ksplice_ud(&run_ud);
			ud_set_input_hook(&run_ud, next_run_byte);
			ud_set_user_opaque_data(&run_ud, (unsigned char *)run);
			safety_start = run;
			if (ud_disassemble(&run_ud) == 0) {
				ret = NO_MATCH;
				goto out;
			}
		}
		run_offset = run - run_start;
		run_unconditional = is_unconditional_jump(&run_ud);
		run_nop = true;
		pre_nop = true;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20) && \
    defined(_I386_BUG_H) && (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11) || \
			     defined(CONFIG_DEBUG_BUGVERBOSE))
/* 91768d6c2bad0d2766a166f13f2f57e197de3458 was after 2.6.19 */
/* 38326f786cf4529a86b1ccde3aa17f4fa7e8472a was after 2.6.10 */
		if (run_ud.mnemonic == pre_ud.mnemonic &&
		    run_ud.mnemonic == UD_Iud2) {
			const struct ksplice_reloc *r;
			/* ud2 means BUG().  On old i386 kernels, it is followed
			   by 2 bytes and then a 4-byte relocation; and is not
			   disassembler-friendly. */
			ret = lookup_reloc(pack, &finger,
					   (unsigned long)(pre + 4), &r);
			if (ret == NO_MATCH) {
				if (mode == RUN_PRE_INITIAL)
					ksdebug(pack, "Unrecognized ud2\n");
				goto out;
			}
			if (ret != OK)
				goto out;
			ret = handle_reloc(pack, sect, r,
					   (unsigned long)(run + 4), mode);
			if (ret != OK)
				goto out;
			/* If there's a relocation, then it's a BUG? */
			if (mode == RUN_PRE_DEBUG) {
				ksdebug(pack, "[BUG?: ");
				print_bytes(pack, run + 2, 6, pre + 2, 6);
				ksdebug(pack, "] ");
			}
			pre += 8;
			run += 8;
			ud_input_skip(&run_ud, 6);
			ud_input_skip(&pre_ud, 6);
			continue;
		}
#endif /* LINUX_VERSION_CODE && _I386_BUG_H && CONFIG_DEBUG_BUGVERBOSE */
#ifdef CONFIG_XEN
		if (run_ud.mnemonic == pre_ud.mnemonic &&
		    run_ud.mnemonic == UD_Iud2) {
			unsigned char bytes[3];
			unsigned char prefix[3] = { 0x78, 0x65, 0x63 };
			if (probe_kernel_read(bytes, (void *)run + 2, 3) !=
			    -EFAULT && pre - pre_start < sect->size &&
			    memcmp(bytes, prefix, 3) == 0 &&
			    memcmp(pre + 2, prefix, 3) == 0) {
				/* Exception for XEN_EMULATE_PREFIX */
				run += 5;
				pre += 5;
				ud_input_skip(&run_ud, 3);
				ud_input_skip(&pre_ud, 3);
				continue;
			}
		}
#endif /* CONFIG_XEN */

		ret = compare_instructions(pack, sect, &finger, run_start, run,
					   pre, &run_ud, &pre_ud, mode);
		if (ret != OK)
			goto out;
		run += ud_insn_len(&run_ud);
		pre += ud_insn_len(&pre_ud);
	}
out:
	if (ret != OK || mode != RUN_PRE_FINAL) {
		vfree(match_map);
		sect->match_map = NULL;
	}
	return ret;
}

static abort_t compare_instructions(struct ksplice_pack *pack,
				    struct ksplice_section *sect,
				    const struct ksplice_reloc **fingerp,
				    const unsigned char *run_start,
				    const unsigned char *run,
				    const unsigned char *pre, struct ud *run_ud,
				    struct ud *pre_ud, enum run_pre_mode mode)
{
	abort_t ret;
	int i;
	const unsigned char *pre_start = (const unsigned char *)sect->address;
	unsigned long pre_offset = pre - pre_start;
	const struct ksplice_reloc *r;

	if (mode == RUN_PRE_DEBUG) {
		ksdebug(pack, "| ");
		print_bytes(pack, run, ud_insn_len(run_ud), pre,
			    ud_insn_len(pre_ud));
	}

	if (run_ud->mnemonic != pre_ud->mnemonic) {
		if (mode == RUN_PRE_DEBUG)
			ksdebug(pack, "mnemonic mismatch: %s %s\n",
				ud_lookup_mnemonic(run_ud->mnemonic),
				ud_lookup_mnemonic(pre_ud->mnemonic));
		return NO_MATCH;
	}

	if (run_ud->mnemonic == UD_Iinvalid) {
		ksdebug(pack, "Unrecognized opcode at %s+%lx\n",
			sect->symbol->label, pre_offset);
		return UNEXPECTED;
	}

	ret = lookup_reloc(pack, fingerp, (unsigned long)pre, &r);
	if (ret == OK && (r->howto->type == KSPLICE_HOWTO_EXTABLE ||
			  r->howto->type == KSPLICE_HOWTO_BUG)) {
		if (mode == RUN_PRE_DEBUG) {
			if (r->howto->type == KSPLICE_HOWTO_EXTABLE)
				ksdebug(pack, "[ex] ");
			if (r->howto->type == KSPLICE_HOWTO_BUG)
				ksdebug(pack, "[bug] ");
		}
		ret = handle_reloc(pack, sect, r, (unsigned long)run, mode);
		if (ret != OK)
			return ret;
	} else if (ret != NO_MATCH && ret != OK) {
		return ret;
#if defined(CONFIG_X86_64) && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
/* 91768d6c2bad0d2766a166f13f2f57e197de3458 was after 2.6.19 */
#else /* !CONFIG_X86_64 || LINUX_VERSION_CODE >= */
	} else if (run_ud->mnemonic == UD_Iud2) {
		ksdebug(pack, "Unexpected ud2\n");
		return NO_MATCH;
#endif /* CONFIG_X86_64 && LINUX_VERSION_CODE */
	}
	for (i = 0; i < ARRAY_SIZE(run_ud->operand); i++) {
		ret = compare_operands(pack, sect, fingerp, run_start, run, pre,
				       run_ud, pre_ud, i, mode);
		if (ret != OK)
			return ret;
	}
	return OK;
}

static abort_t compare_operands(struct ksplice_pack *pack,
				struct ksplice_section *sect,
				const struct ksplice_reloc **fingerp,
				const unsigned char *run_start,
				const unsigned char *run,
				const unsigned char *pre, struct ud *run_ud,
				struct ud *pre_ud, int opnum,
				enum run_pre_mode mode)
{
	abort_t ret;
	int i;
	const unsigned char *pre_start = (const unsigned char *)sect->address;
	unsigned long pre_offset = pre - pre_start;
	unsigned long run_offset = run - run_start;
	struct ud_operand *run_op = &run_ud->operand[opnum];
	struct ud_operand *pre_op = &pre_ud->operand[opnum];
	uint8_t run_off = ud_prefix_len(run_ud);
	uint8_t pre_off = ud_prefix_len(pre_ud);
	const unsigned char **match_map = sect->match_map;
	const struct ksplice_reloc *r;
	for (i = 0; i < opnum; i++) {
		run_off += ud_operand_len(&run_ud->operand[i]);
		pre_off += ud_operand_len(&pre_ud->operand[i]);
	}

	if (run_op->type != pre_op->type) {
		if (mode == RUN_PRE_DEBUG)
			ksdebug(pack, "type mismatch: %d %d\n", run_op->type,
				pre_op->type);
		return NO_MATCH;
	}
	if (run_op->base != pre_op->base) {
		if (mode == RUN_PRE_DEBUG)
			ksdebug(pack, "base mismatch: %d %d\n", run_op->base,
				pre_op->base);
		return NO_MATCH;
	}
	if (run_op->index != pre_op->index) {
		if (mode == RUN_PRE_DEBUG)
			ksdebug(pack, "index mismatch: %d %d\n",
				run_op->index, pre_op->index);
		return NO_MATCH;
	}
	if (run_op->type == UD_OP_PTR &&
	    run_op->lval.ptr.seg != pre_op->lval.ptr.seg) {
		if (mode == RUN_PRE_DEBUG)
			ksdebug(pack, "segment mismatch: %d %d\n",
				run_op->lval.ptr.seg, pre_op->lval.ptr.seg);
		return NO_MATCH;
	}
	if (ud_operand_len(run_op) == 0 && ud_operand_len(pre_op) == 0)
		return OK;

	ret = lookup_reloc(pack, fingerp, (unsigned long)(pre + pre_off), &r);
	if (ret == OK) {
		struct ksplice_reloc run_reloc = *r;
		struct ksplice_reloc_howto run_howto = *r->howto;
		unsigned int run_reloc_len = ud_operand_len(run_op);
		unsigned int pre_reloc_len = ud_operand_len(pre_op);

		if (run_op->type == UD_OP_PTR) {
			/* Adjust for reloc length != operand length for
			   instructions take a segment:offset operand */
			run_reloc_len -= 2;
			pre_reloc_len -= 2;
		}

		run_reloc.howto = &run_howto;
		if (r->howto->size != pre_reloc_len) {
			ksdebug(pack, "ksplice_h: run-pre: reloc size %d "
				"differs from disassembled size %d\n",
				r->howto->size, pre_reloc_len);
			return NO_MATCH;
		}
		if (r->howto->size != run_reloc_len &&
		    (r->howto->dst_mask != N_BITS(r->howto->size * 8) ||
		     r->howto->rightshift != 0)) {
			/* Reloc types unsupported with differing reloc sizes */
			ksdebug(pack, "ksplice_h: reloc: invalid flags for a "
				"relocation with size changed\n");
			ksdebug(pack, "%ld %u\n", r->howto->dst_mask,
				r->howto->rightshift);
			return UNEXPECTED;
		}
		/* adjust for differing relocation size */
		run_howto.size = run_reloc_len;
		if (r->howto->size != run_howto.size)
			run_howto.dst_mask = N_BITS(run_howto.size * 8);
		run_reloc.insn_addend += pre_reloc_len - run_reloc_len;
		ret = handle_reloc(pack, sect, &run_reloc,
				   (unsigned long)(run + run_off), mode);
		if (ret != OK) {
			if (mode == RUN_PRE_DEBUG)
				ksdebug(pack, "Matching failure at offset "
					"%lx\n", pre_offset);
			return ret;
		}
		/* This operand is a successfully processed relocation */
		return OK;
	} else if (ret != NO_MATCH) {
		return ret;
	}
	if (pre_op->type == UD_OP_JIMM) {
		/* Immediate jump without a relocation */
		const unsigned char *pre_target = pre + ud_insn_len(pre_ud) +
		    ud_operand_lval(pre_op);
		const unsigned char *run_target = run + ud_insn_len(run_ud) +
		    ud_operand_lval(run_op);
		if (pre_target == run_target) {
			/* Paravirt-inserted pcrel jump; OK! */
			return OK;
		} else if (pre_target >= pre_start &&
			   pre_target < pre_start + sect->size) {
			/* Jump within the current function.
			   Check it's to a corresponding place */
			unsigned long new_pre_offset = pre_target - pre_start;
			unsigned long new_run_offset = run_target - run_start;
			if (mode == RUN_PRE_DEBUG)
				ksdebug(pack, "[Jumps: pre=%lx run=%lx "
					"pret=%lx runt=%lx] ", pre_offset,
					run_offset, new_pre_offset,
					new_run_offset);
			if (match_map[pre_target - pre_start] != NULL &&
			    match_map[pre_target - pre_start] != run_target) {
				ksdebug(pack, "<--[Jumps to nonmatching "
					"locations]\n");
				return NO_MATCH;
			} else if (match_map[pre_target - pre_start] == NULL) {
				match_map[pre_target - pre_start] = run_target;
			}
			return OK;
		} else {
			if (mode == RUN_PRE_DEBUG) {
				ksdebug(pack, "<--Different operands!\n");
				ksdebug(pack, "%lx %lx %lx %lx %x %lx %lx "
					"%lx\n", (unsigned long)pre_start,
					(unsigned long)pre_target,
					(unsigned long)pre_start + sect->size,
					(unsigned long)pre, ud_insn_len(pre_ud),
					sect->size, ud_operand_lval(pre_op),
					(unsigned long)run_target);
			}
			return NO_MATCH;
		}
	} else if (ud_operand_len(pre_op) == ud_operand_len(run_op) &&
		   memcmp(pre + pre_off, run + run_off,
			  ud_operand_len(run_op)) == 0) {
		return OK;
	} else {
		if (mode == RUN_PRE_DEBUG)
			ksdebug(pack, "<--Different operands!\n");
		return NO_MATCH;
	}
}

static void initialize_ksplice_ud(struct ud *ud)
{
	ud_init(ud);
	ud_set_mode(ud, BITS_PER_LONG);
	ud_set_syntax(ud, NULL);
	ud_set_pc(ud, 0);
	ud_set_vendor(ud, UD_VENDOR_ANY);
}

#ifdef CONFIG_FTRACE
static bool is_mcount_call(struct ud *ud, unsigned long addr)
{
	unsigned long target = addr + ud_insn_len(ud) +
	    ud_operand_lval(&ud->operand[0]);
	if (ud->mnemonic == UD_Icall &&
	    (target == (unsigned long)mcount ||
	     target == (unsigned long)ftrace_trace_function))
		return true;
	return false;
}
#else /* !CONFIG_FTRACE */
static bool is_mcount_call(struct ud *ud, unsigned long addr)
{
	return false;
}
#endif /* CONFIG_FTRACE */

static bool is_nop(struct ud *ud)
{
	switch (ud->mnemonic) {
	case UD_Inop:
		return true;
	case UD_Imov:
		return ud->operand[0].type == UD_OP_REG &&
		    ud->operand[1].type == UD_OP_REG &&
		    ud->operand[2].type == UD_NONE &&
		    ud->operand[0].base == ud->operand[1].base;
	case UD_Ixchg:
		return ud->operand[0].type == UD_OP_REG &&
		    ud->operand[1].type == UD_OP_REG &&
		    ud->operand[2].type == UD_NONE &&
		    ud->operand[0].base == ud->operand[1].base;
	case UD_Ilea:
		return ud->operand[0].type == UD_OP_REG &&
		    ud->operand[1].type == UD_OP_MEM &&
		    ((ud->operand[1].base == ud->operand[0].base &&
		      ud->operand[1].index == UD_NONE) ||
		     (ud->operand[1].base == UD_NONE &&
		      ud->operand[1].index == ud->operand[0].base &&
		      ud->operand[1].scale == 0)) &&
		    ud_operand_lval(&ud->operand[1]) == 0 &&
		    ud->operand[2].type == UD_NONE;
	default:
		return false;
	}
}

static bool is_unconditional_jump(struct ud *ud)
{
	switch (ud->mnemonic) {
	case UD_Ijmp:
	case UD_Iret:
	case UD_Iretf:
	case UD_Iiretw:
	case UD_Iiretd:
	case UD_Iiretq:
	case UD_Isysexit:
	case UD_Isysret:
	case UD_Isyscall:
	case UD_Isysenter:
		return true;
	default:
		return false;
	}
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

static long ud_operand_lval(struct ud_operand *operand)
{
	switch (operand->type == UD_OP_MEM ? operand->offset : operand->size) {
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

static int next_run_byte(struct ud *ud)
{
	unsigned char byte;
	if (probe_kernel_read(&byte, ud_get_user_opaque_data(ud), 1) == -EFAULT)
		return UD_EOI;
	ud_set_user_opaque_data(ud, ud_get_user_opaque_data(ud) + 1);
	return byte;
}
#endif /* !CONFIG_FUNCTION_DATA_SECTIONS */

static struct ksplice_symbol trampoline_symbol = {
	.name = NULL,
	.label = "<trampoline>",
};

static const struct ksplice_reloc_howto trampoline_howto = {
	.type = KSPLICE_HOWTO_RELOC,
	.pcrel = 1,
	.size = 4,
	.dst_mask = 0xffffffffL,
	.rightshift = 0,
	.signed_addend = 1,
};

static const struct ksplice_reloc trampoline_reloc = {
	.symbol = &trampoline_symbol,
	.insn_addend = -4,
	.target_addend = 0,
	.howto = &trampoline_howto,
};

static abort_t trampoline_target(struct ksplice_pack *pack, unsigned long addr,
				 unsigned long *new_addr)
{
	abort_t ret;
	unsigned char byte;

	if (probe_kernel_read(&byte, (void *)addr, sizeof(byte)) == -EFAULT)
		return NO_MATCH;

	if (byte != 0xe9)
		return NO_MATCH;

	ret = read_reloc_value(pack, &trampoline_reloc, addr + 1, new_addr);
	if (ret != OK)
		return ret;

	*new_addr += addr + 1;
	return OK;
}

static abort_t prepare_trampoline(struct ksplice_pack *pack,
				  struct ksplice_patch *p)
{
	p->size = 5;
	((unsigned char *)p->contents)[0] = 0xe9;
	return write_reloc_value(pack, &trampoline_reloc,
				 (unsigned long)p->contents + 1,
				 p->repladdr - (p->oldaddr + 1));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
static abort_t handle_bug(struct ksplice_pack *pack,
			  const struct ksplice_reloc *r, unsigned long run_addr)
{
	const struct bug_entry *run_bug = find_bug(run_addr);
	struct ksplice_section *bug_sect = symbol_section(pack, r->symbol);
	if (run_bug == NULL)
		return NO_MATCH;
	if (bug_sect == NULL)
		return UNEXPECTED;
	return create_labelval(pack, bug_sect->symbol, (unsigned long)run_bug,
			       TEMP);
}
#else /* LINUX_VERSION_CODE < */
/* 7664c5a1da4711bb6383117f51b94c8dc8f3f1cd was after 2.6.19 */
static abort_t handle_bug(struct ksplice_pack *pack,
			  const struct ksplice_reloc *r, unsigned long run_addr)
{
	/* There should be no bug_table, so just return NO_MATCH */
	return NO_MATCH;
}
#endif /* LINUX_VERSION_CODE */

static abort_t handle_extable(struct ksplice_pack *pack,
			      const struct ksplice_reloc *r,
			      unsigned long run_addr)
{
	const struct exception_table_entry *run_ent =
	    search_exception_tables(run_addr);
	struct ksplice_section *ex_sect = symbol_section(pack, r->symbol);
	if (run_ent == NULL)
		return NO_MATCH;
	if (ex_sect == NULL)
		return UNEXPECTED;
	return create_labelval(pack, ex_sect->symbol, (unsigned long)run_ent,
			       TEMP);
}

static abort_t handle_paravirt(struct ksplice_pack *pack,
			       unsigned long pre_addr, unsigned long run_addr,
			       int *matched)
{
	unsigned char run[5], pre[5];
	*matched = 0;

	if (probe_kernel_read(&run, (void *)run_addr, sizeof(run)) == -EFAULT ||
	    probe_kernel_read(&pre, (void *)pre_addr, sizeof(pre)) == -EFAULT)
		return OK;

	if ((run[0] == 0xe8 && pre[0] == 0xe8) ||
	    (run[0] == 0xe9 && pre[0] == 0xe9))
		if (run_addr + 1 + *(int32_t *)&run[1] ==
		    pre_addr + 1 + *(int32_t *)&pre[1])
			*matched = 5;
	return OK;
}

static bool valid_stack_ptr(const struct thread_info *tinfo, const void *p)
{
	return p > (const void *)tinfo
	    && p <= (const void *)tinfo + THREAD_SIZE - sizeof(long);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static bool virtual_address_mapped(unsigned long addr)
{
	pgd_t *pgd;
#ifdef pud_page
	pud_t *pud;
#endif /* pud_page */
	pmd_t *pmd;
	pte_t *pte;

#ifdef KSPLICE_STANDALONE
	if (!bootstrapped)
		return true;
#endif /* KSPLICE_STANDALONE */

	pgd = pgd_offset_k(addr);
	if (!pgd_present(*pgd))
		return false;

#ifdef pud_page
	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		return false;

	pmd = pmd_offset(pud, addr);
#else /* pud_page */
	pmd = pmd_offset(pgd, addr);
#endif /* pud_page */

	if (!pmd_present(*pmd))
		return false;

	if (pmd_large(*pmd))
		return true;

	pte = pte_offset_kernel(pmd, addr);
	if (!pte_present(*pte))
		return false;

	return true;
}
#endif /* LINUX_VERSION_CODE */
