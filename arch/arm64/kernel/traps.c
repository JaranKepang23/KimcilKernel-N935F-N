/*
 * Based on arch/arm/kernel/traps.c
 *
 * Copyright (C) 1995-2009 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/exynos-ss.h>

#include <asm/atomic.h>
#include <asm/debug-monitors.h>
#include <asm/traps.h>
#include <asm/esr.h>
#include <asm/stacktrace.h>
#include <asm/exception.h>
#include <asm/system_misc.h>
#ifdef CONFIG_SEC_DEBUG
#include <linux/sec_debug.h>
#endif

#ifdef CONFIG_RKP_CFP_ROPP
#include <linux/rkp_cfp.h>
#endif
static const char *handler[]= {
	"Synchronous Abort",
	"IRQ",
	"FIQ",
	"Error"
};

int show_unhandled_signals = 1;

/*
 * Dump out the contents of some memory nicely...
 */
static void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk("%s%s(0x%016lx to 0x%016lx)\n", lvl, str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < 8 && p < top; i++, p += 4) {
			if (p >= bottom && p < top) {
				unsigned int val;
				if (__get_user(val, (unsigned int *)p) == 0)
					sprintf(str + i * 9, " %08x", val);
				else
					sprintf(str + i * 9, " ????????");
			}
		}
		printk("%s%04lx:%s\n", lvl, first & 0xffff, str);
	}

	set_fs(fs);
}

static void dump_backtrace_entry(unsigned long where, unsigned long stack)
{
	print_ip_sym(where);
	if (in_exception_text(where))
		dump_mem("", "Exception stack", stack,
			 stack + sizeof(struct pt_regs));
}

#ifdef CONFIG_SEC_DEBUG_AUTO_SUMMARY
static void dump_backtrace_entry_auto_summary(unsigned long where, unsigned long stack)
{
	pr_auto(ASL2, "[<%p>] %pS\n", (void *)where, (void *)where);

	if (in_exception_text(where))
		dump_mem("", "Exception stack", stack,
			 stack + sizeof(struct pt_regs));
}
#endif

static void dump_instr(const char *lvl, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	mm_segment_t fs;
	char str[sizeof("00000000 ") * 5 + 2 + 1], *p = str;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	for (i = -4; i < 1; i++) {
		unsigned int val, bad;

		bad = __get_user(val, &((u32 *)addr)[i]);

		if (!bad)
			p += sprintf(p, i == 0 ? "(%08x) " : "%08x ", val);
		else {
			p += sprintf(p, "bad PC value");
			break;
		}
	}
	printk("%sCode: %s\n", lvl, str);

	set_fs(fs);
}

static void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	struct stackframe frame;
#ifdef CONFIG_RKP_CFP_ROPP
	unsigned long init_pc = 0x0, rrk = 0x0;
#endif

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (!tsk)
		tsk = current;

	if (regs) {
		frame.fp = regs->regs[29];
		frame.sp = regs->sp;
		frame.pc = regs->pc;
	} else if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.pc = (unsigned long)dump_backtrace;
	} else {
		/*
		 * task blocked in __switch_to
		 */
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		frame.pc = thread_saved_pc(tsk);
	}

#ifdef CONFIG_RKP_CFP_ROPP
	init_pc = frame.pc;
#ifdef CONFIG_RKP_CFP_ROPP_HYPKEY
	rkp_call(CFP_ROPP_RET_KEY, (unsigned long) &(task_thread_info(tsk)->rrk), 0, 0, 0, 0);
	asm("mov %0, x16" : "=r" (rrk));
#else //CONFIG_RKP_CFP_ROPP_HYPKEY
	rrk = task_thread_info(tsk)->rrk;
#endif //CONFIG_RKP_CFP_ROPP_HYPKEY
#endif //CONFIG_RKP_CFP_ROPP

	pr_emerg("Call trace:\n");

	while (1) {
		unsigned long where = frame.pc;
		int ret;

		ret = unwind_frame(&frame);
		if (ret < 0)
			break;
#ifdef CONFIG_RKP_CFP_ROPP
        if ((where != init_pc) && (0x1 == dump_stack_dec)){
            where = where ^ rrk;
        }
#endif
		dump_backtrace_entry(where, frame.sp);

	}
}

#ifdef CONFIG_SEC_DEBUG_AUTO_SUMMARY
static void dump_backtrace_auto_summary(struct pt_regs *regs, struct task_struct *tsk)
{
	struct stackframe frame;
#ifdef CONFIG_RKP_CFP_ROPP
	unsigned long init_pc = 0x0, rrk = 0x0;
#endif

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (!tsk)
		tsk = current;

	if (regs) {
		frame.fp = regs->regs[29];
		frame.sp = regs->sp;
		frame.pc = regs->pc;
	} else if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.pc = (unsigned long)dump_backtrace;
	} else {
		/*
		 * task blocked in __switch_to
		 */
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		frame.pc = thread_saved_pc(tsk);
	}

#ifdef CONFIG_RKP_CFP_ROPP
	init_pc = frame.pc;
#ifdef CONFIG_RKP_CFP_ROPP_HYPKEY
	rkp_call(CFP_ROPP_RET_KEY, (unsigned long) &(task_thread_info(tsk)->rrk), 0, 0, 0, 0);
	asm("mov %0, x16" : "=r" (rrk));
#else //CONFIG_RKP_CFP_ROPP_HYPKEY
	rrk = task_thread_info(tsk)->rrk;
#endif //CONFIG_RKP_CFP_ROPP_HYPKEY
#endif //CONFIG_RKP_CFP_ROPP

	pr_auto_once(2);
	pr_auto(ASL2, "Call trace:\n");

	while (1) {
		unsigned long where = frame.pc;
		int ret;

		ret = unwind_frame(&frame);
		if (ret < 0)
			break;
#ifdef CONFIG_RKP_CFP_ROPP
		if ((where != init_pc) && (0x1 == dump_stack_dec)){
			where = where ^ rrk;
		}
#endif
		dump_backtrace_entry_auto_summary(where, frame.sp);
	}
}
#endif

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	dump_backtrace(NULL, tsk);
	barrier();
}

#ifdef CONFIG_PREEMPT
#define S_PREEMPT " PREEMPT"
#else
#define S_PREEMPT ""
#endif
#ifdef CONFIG_SMP
#define S_SMP " SMP"
#else
#define S_SMP ""
#endif

static int __die(const char *str, int err, struct thread_info *thread,
		 struct pt_regs *regs)
{
	struct task_struct *tsk = thread->task;
	static int die_counter;
	int ret;

	pr_emerg("Internal error: %s: %x [#%d]" S_PREEMPT S_SMP "\n",
		 str, err, ++die_counter);

	/* trap and error numbers are mostly meaningless on ARM */
	ret = notify_die(DIE_OOPS, str, regs, err, 0, SIGSEGV);
	if (ret == NOTIFY_STOP)
		return ret;

	print_modules();
	__show_regs(regs);
	pr_emerg("Process %.*s (pid: %d, stack limit = 0x%p)\n",
		 TASK_COMM_LEN, tsk->comm, task_pid_nr(tsk), thread + 1);

	if (!user_mode(regs) || in_interrupt()) {
		dump_mem(KERN_EMERG, "Stack: ", regs->sp,
			 THREAD_SIZE + (unsigned long)task_stack_page(tsk));

#ifdef CONFIG_SEC_DEBUG_AUTO_SUMMARY		
		dump_backtrace_auto_summary(NULL, tsk);
#else
		dump_backtrace(NULL, tsk);
#endif
		dump_instr(KERN_EMERG, regs);
	}
	return ret;
}

static DEFINE_RAW_SPINLOCK(die_lock);

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	enum bug_trap_type bug_type = BUG_TRAP_TYPE_NONE;
	struct thread_info *thread = current_thread_info();
	int ret;

	oops_enter();

	raw_spin_lock_irq(&die_lock);
	console_verbose();
	bust_spinlocks(1);

	if (!user_mode(regs)) {
		/*
		 * If recalling hardlockup core has been run before,
		 * PC value must be replaced to real PC value.
		 */
		exynos_ss_hook_hardlockup_entry((void *)regs);
		bug_type = report_bug(regs->pc, regs);
	}
	if (bug_type != BUG_TRAP_TYPE_NONE)
		str = "Oops - BUG";

	ret = __die(str, err, thread, regs);

	if (regs && kexec_should_crash(thread->task))
		crash_kexec(regs);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	raw_spin_unlock_irq(&die_lock);
	oops_exit();

#ifdef CONFIG_SEC_DEBUG_EXTRA_INFO
	sec_debug_set_extra_info_backtrace(regs);
#endif

#if defined(CONFIG_SEC_DEBUG)
	if (in_interrupt())
		panic("%s\nPC is at %pS\nLR is at %pS",
				"Fatal exception in interrupt", (void *)regs->pc,
				compat_user_mode(regs) ? (void *)regs->compat_lr : (void *)regs->regs[30]);
	if (panic_on_oops)
		panic("%s\nPC is at %pS\nLR is at %pS",
				"Fatal exception", (void *)regs->pc,
				compat_user_mode(regs) ? (void *)regs->compat_lr : (void *)regs->regs[30]);
#else
	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
#endif

	if (ret != NOTIFY_STOP)
		do_exit(SIGSEGV);
}

void arm64_notify_die(const char *str, struct pt_regs *regs,
		      struct siginfo *info, int err)
{
	if (user_mode(regs)) {
		current->thread.fault_address = 0;
		current->thread.fault_code = err;
		force_sig_info(info->si_signo, info, current);
	} else {
		die(str, regs, err);
	}
}

static LIST_HEAD(undef_hook);
static DEFINE_RAW_SPINLOCK(undef_lock);

void register_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_add(&hook->node, &undef_hook);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

void unregister_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_del(&hook->node);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

static int call_undef_hook(struct pt_regs *regs)
{
	struct undef_hook *hook;
	unsigned long flags;
	u32 instr;
	int (*fn)(struct pt_regs *regs, u32 instr) = NULL;
	void __user *pc = (void __user *)instruction_pointer(regs);

	if (!user_mode(regs))
		return 1;

	if (compat_thumb_mode(regs)) {
		/* 16-bit Thumb instruction */
		if (get_user(instr, (u16 __user *)pc))
			goto exit;
		instr = le16_to_cpu(instr);
		if (aarch32_insn_is_wide(instr)) {
			u32 instr2;

			if (get_user(instr2, (u16 __user *)(pc + 2)))
				goto exit;
			instr2 = le16_to_cpu(instr2);
			instr = (instr << 16) | instr2;
		}
	} else {
		/* 32-bit ARM instruction */
		if (get_user(instr, (u32 __user *)pc))
			goto exit;
		instr = le32_to_cpu(instr);
	}

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_for_each_entry(hook, &undef_hook, node)
		if ((instr & hook->instr_mask) == hook->instr_val &&
			(regs->pstate & hook->pstate_mask) == hook->pstate_val)
			fn = hook->fn;

	raw_spin_unlock_irqrestore(&undef_lock, flags);
exit:
	return fn ? fn(regs, instr) : 1;
}

#ifdef CONFIG_GENERIC_BUG
int is_valid_bugaddr(unsigned long pc)
{
	return 1;
}
#endif

asmlinkage void __exception do_undefinstr(struct pt_regs *regs)
{
	siginfo_t info;
	void __user *pc = (void __user *)instruction_pointer(regs);

	/* check for AArch32 breakpoint instructions */
	if (!aarch32_break_handler(regs))
		return;

	if (call_undef_hook(regs) == 0)
		return;

	if (show_unhandled_signals && unhandled_signal(current, SIGILL) &&
	    printk_ratelimit()) {
		pr_info("%s[%d]: undefined instruction: pc=%p\n",
			current->comm, task_pid_nr(current), pc);
		dump_instr(KERN_INFO, regs);
	}

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

#ifdef CONFIG_SEC_DEBUG_EXTRA_INFO
	if (!user_mode(regs))
		sec_debug_set_extra_info_fault(-1, regs);
#endif

	arm64_notify_die("Oops - undefined instruction", regs, &info, 0);
}

long compat_arm_syscall(struct pt_regs *regs);

asmlinkage long do_ni_syscall(struct pt_regs *regs)
{
#ifdef CONFIG_COMPAT
	long ret;
	if (is_compat_task()) {
		ret = compat_arm_syscall(regs);
		if (ret != -ENOSYS)
			return ret;
	}
#endif

	if (show_unhandled_signals && printk_ratelimit()) {
		pr_info("%s[%d]: syscall %d\n", current->comm,
			task_pid_nr(current), (int)regs->syscallno);
		dump_instr("", regs);
		if (user_mode(regs))
			__show_regs(regs);
	}

	return sys_ni_syscall();
}

static const char *esr_class_str[] = {
	[0 ... ESR_EL1_EC_MAX]		= "UNRECOGNIZED EC",
	[ESR_EL1_EC_UNKNOWN]		= "Unknown/Uncategorized",
	[ESR_EL1_EC_WFI]		= "WFI/WFE",
	[ESR_EL1_EC_CP15_32]		= "CP15 MCR/MRC",
	[ESR_EL1_EC_CP15_64]		= "CP15 MCRR/MRRC",
	[ESR_EL1_EC_CP14_MR]		= "CP14 MCR/MRC",
	[ESR_EL1_EC_CP14_LS]		= "CP14 LDC/STC",
	[ESR_EL1_EC_FP_ASIMD]		= "ASIMD",
	[ESR_EL1_EC_CP10_ID]		= "CP10 MRC/VMRS",
	[ESR_EL1_EC_CP14_64]		= "CP14 MCRR/MRRC",
	[ESR_EL1_EC_ILL_ISS]		= "PSTATE.IL",
	[ESR_EL1_EC_SVC32]		= "SVC (AArch32)",
	[ESR_EL1_EC_HVC32]		= "HVC (AArch32)",
	[ESR_EL1_EC_SMC32]		= "SMC (AArch32)",
	[ESR_EL1_EC_SVC64]		= "SVC (AArch64)",
	[ESR_EL1_EC_HVC64]		= "HVC (AArch64)",
	[ESR_EL1_EC_SMC64]		= "SMC (AArch64)",
	[ESR_EL1_EC_SYS64]		= "MSR/MRS (AArch64)",
	[ESR_EL1_EC_IMP_DEF]		= "EL3 IMP DEF",
	[ESR_EL1_EC_IABT_EL0]		= "IABT (lower EL)",
	[ESR_EL1_EC_IABT_EL1]		= "IABT (current EL)",
	[ESR_EL1_EC_PC_ALIGN]		= "PC Alignment",
	[ESR_EL1_EC_DABT_EL0]		= "DABT (lower EL)",
	[ESR_EL1_EC_DABT_EL1]		= "DABT (current EL)",
	[ESR_EL1_EC_SP_ALIGN]		= "SP Alignment",
	[ESR_EL1_EC_FP_EXC32]		= "FP (AArch32)",
	[ESR_EL1_EC_FP_EXC64]		= "FP (AArch64)",
	[ESR_EL1_EC_SERROR]		= "SError",
	[ESR_EL1_EC_BREAKPT_EL0]	= "Breakpoint (lower EL)",
	[ESR_EL1_EC_BREAKPT_EL1]	= "Breakpoint (current EL)",
	[ESR_EL1_EC_SOFTSTP_EL0]	= "Software Step (lower EL)",
	[ESR_EL1_EC_SOFTSTP_EL1]	= "Software Step (current EL)",
	[ESR_EL1_EC_WATCHPT_EL0]	= "Watchpoint (lower EL)",
	[ESR_EL1_EC_WATCHPT_EL1]	= "Watchpoint (current EL)",
	[ESR_EL1_EC_BKPT32]		= "BKPT (AArch32)",
	[ESR_EL1_EC_VECTOR32]		= "Vector catch (AArch32)",
	[ESR_EL1_EC_BRK64]		= "BRK (AArch64)",
};

const char *esr_get_class_string(u32 esr)
{
	return esr_class_str[esr >> ESR_EL1_EC_SHIFT];
}

/*
 * bad_mode handles the impossible case in the exception vector.
 */
asmlinkage void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
	siginfo_t info;
	void __user *pc = (void __user *)instruction_pointer(regs);
	console_verbose();

	pr_auto(ASL1, "Bad mode in %s handler detected, code 0x%08x -- %s\n",
		handler[reason], esr, esr_get_class_string(esr));

	__show_regs(regs);

#ifdef CONFIG_SEC_DEBUG_EXTRA_INFO
	if (!user_mode(regs)) {
		sec_debug_set_extra_info_fault(SEC_DEBUG_BADMODE_MAGIC, regs);
		sec_debug_set_extra_info_esr(esr);
	}
#endif

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

	arm64_notify_die("Oops - bad mode", regs, &info, 0);
}

void __pte_error(const char *file, int line, unsigned long val)
{
	pr_crit("%s:%d: bad pte %016lx.\n", file, line, val);
}

void __pmd_error(const char *file, int line, unsigned long val)
{
	pr_crit("%s:%d: bad pmd %016lx.\n", file, line, val);
}

void __pud_error(const char *file, int line, unsigned long val)
{
	pr_crit("%s:%d: bad pud %016lx.\n", file, line, val);
}

void __pgd_error(const char *file, int line, unsigned long val)
{
	pr_crit("%s:%d: bad pgd %016lx.\n", file, line, val);
}

void __init trap_init(void)
{
	return;
}
