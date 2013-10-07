/*
 *  arch/arm/include/asm/mmu_context.h
 *
 *  Copyright (C) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#include <linux/compiler.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>
#include <asm/cachetype.h>
#include <asm/proc-fns.h>
#include <asm-generic/mm_hooks.h>

void __check_kvm_seq(struct mm_struct *mm);

#ifdef CONFIG_CPU_HAS_ASID

/*
 * On ARMv6, we have the following structure in the Context ID:
 *
 * 31                         7          0
 * +-------------------------+-----------+
 * |      process ID         |   ASID    |
 * +-------------------------+-----------+
 * |              context ID             |
 * +-------------------------------------+
 *
 * The ASID is used to tag entries in the CPU caches and TLBs.
 * The context ID is used by debuggers and trace logic, and
 * should be unique within all running processes.
 */
#define ASID_BITS		8
#define ASID_MASK		((~0) << ASID_BITS)
#define ASID_FIRST_VERSION	(1 << ASID_BITS)

#ifdef CONFIG_TIMA_RKP_DEBUG
extern unsigned long tima_debug_infra_cnt;
#endif

extern unsigned int cpu_last_asid;
#ifdef CONFIG_SMP
DECLARE_PER_CPU(struct mm_struct *, current_mm);
#endif

void __init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void __new_context(struct mm_struct *mm);

static inline void check_context(struct mm_struct *mm)
{
	/*
	 * This code is executed with interrupts enabled. Therefore,
	 * mm->context.id cannot be updated to the latest ASID version
	 * on a different CPU (and condition below not triggered)
	 * without first getting an IPI to reset the context. The
	 * alternative is to take a read_lock on mm->context.id_lock
	 * (after changing its type to rwlock_t).
	 */
	if (unlikely((mm->context.id ^ cpu_last_asid) >> ASID_BITS))
		__new_context(mm);

	if (unlikely(mm->context.kvm_seq != init_mm.context.kvm_seq))
		__check_kvm_seq(mm);
}

#define init_new_context(tsk,mm)	(__init_new_context(tsk,mm),0)

#else

static inline void check_context(struct mm_struct *mm)
{
#ifdef CONFIG_MMU
	if (unlikely(mm->context.kvm_seq != init_mm.context.kvm_seq))
		__check_kvm_seq(mm);
#endif
}

#define init_new_context(tsk,mm)	0

#endif

#define destroy_context(mm)		do { } while(0)

/*
 * This is called when "tsk" is about to enter lazy TLB mode.
 *
 * mm:  describes the currently active mm context
 * tsk: task which is entering lazy tlb
 * cpu: cpu number which is entering lazy tlb
 *
 * tsk->mm will be NULL
 */
static inline void
enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.  We avoid
 * calling the CPU specific function when the mm hasn't
 * actually changed.
 */
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
	
#ifdef CONFIG_TIMA_RKP_DEBUG
	int i;
	unsigned long pmd;
	unsigned long va;
	int ret;
#endif 
#ifdef CONFIG_MMU
	unsigned int cpu = smp_processor_id();

#ifdef CONFIG_SMP
	/* check for possible thread migration */
	if (!cpumask_empty(mm_cpumask(next)) &&
	    !cpumask_test_cpu(cpu, mm_cpumask(next)))
		__flush_icache_all();
#endif
	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) || prev != next) {
#ifdef CONFIG_SMP
		struct mm_struct **crt_mm = &per_cpu(current_mm, cpu);
		*crt_mm = next;
#endif
		check_context(next);
		cpu_switch_mm(next->pgd, next);
	#ifdef CONFIG_TIMA_RKP_DEBUG
		/* 
		 * if debug infrastructure is enabled,
		 * check is L1 and L2 page tables of a 
		 * process are protected (readonly) at 
		 * each context switch
		 */
		#ifdef CONFIG_TIMA_RKP_L1_TABLES
		for (i=0; i<4; i++) {
			if (tima_debug_page_protection(((unsigned long)next->pgd + i*0x1000), 1, 1) == 0) {
				tima_debug_signal_failure(0x3f80f221, 1);
				//tima_send_cmd((unsigned long)next->pgd, 0x3f80e221);
				//printk(KERN_ERR"TIMA: New L1 PGT not protected\n");
			}
		}
		#endif
		#ifdef CONFIG_TIMA_RKP_L2_TABLES
		for (i=0; i<0x1000; i++) {
			pmd = *(unsigned long *)((unsigned long)next->pgd + i*4);
			if ((pmd & 0x3) != 0x1)
				continue;
			if((0x07e00000 <= pmd) && (pmd <= 0x07f00000)) /* skip sect to pgt region */
			       continue;	
			va = (unsigned long)phys_to_virt(pmd & (~0x3ff)) ;
			if ((ret = tima_debug_page_protection(va, 0x101, 1)) == 0) {
				tima_debug_signal_failure(0x3f80f221, 101);
				//printk(KERN_ERR"TIMA: New L2 PGT not RO va=%lx pa=%lx tima_debug_infra_cnt=%lx ret=%d\n", va, pmd, tima_debug_infra_cnt, ret);
			} else if (ret == 1) {
				tima_debug_infra_cnt++;
			}
		}
		#endif /* CONFIG_TIMA_RKP_L2_TABLES */
	#endif /* CONFIG_TIMA_RKP_DEBUG */
		if (cache_is_vivt())
			cpumask_clear_cpu(cpu, mm_cpumask(prev));
	}
#endif
}

#define deactivate_mm(tsk,mm)	do { } while (0)
#define activate_mm(prev,next)	switch_mm(prev, next, NULL)

#endif
