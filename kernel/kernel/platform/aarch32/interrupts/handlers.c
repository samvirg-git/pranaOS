/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drivers/aarch32/gicv2.h>
#include <libkern/libkern.h>
#include <libkern/log.h>
#include <mem/vmm/vmm.h>
#include <platform/aarch32/interrupts.h>
#include <platform/aarch32/system.h>
#include <platform/aarch32/tasking/trapframe.h>
#include <platform/generic/registers.h>
#include <syscalls/handlers.h>
#include <tasking/cpu.h>
#include <tasking/dump.h>
#include <tasking/tasking.h>

#define ERR_BUF_SIZE 64
static char err_buf[ERR_BUF_SIZE];

static gic_descritptor_t gic_descriptor;

/* IRQ */
static irq_handler_t _irq_handlers[IRQ_HANDLERS_MAX];
static void _irq_empty_handler();
static inline void _irq_redirect(int int_no);
static void init_irq_handlers();

static inline uint32_t is_interrupt_enabled()
{
    return ((read_cpsr() >> 7) & 1) == 0;
}

void interrupts_setup()
{
    system_disable_interrupts();
    system_enable_interrupts_only_counter(); // Reset counter
    set_abort_stack((uint32_t)&STACK_ABORT_TOP);
    set_undefined_stack((uint32_t)&STACK_UNDEFINED_TOP);
    set_svc_stack((uint32_t)&STACK_SVC_TOP);
    set_irq_stack((uint32_t)&STACK_IRQ_TOP);
    init_irq_handlers();
}

static uint32_t new_zone_for_secondary_cpu()
{
    zone_t zone = zoner_new_zone(VMM_PAGE_SIZE);
    vmm_load_page(zone.start, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE);
    return zone.start + zone.len;
}

void interrupts_setup_secondary_cpu()
{
    system_disable_interrupts();
    system_enable_interrupts_only_counter(); // Reset counter
    set_abort_stack(new_zone_for_secondary_cpu());
    set_undefined_stack(new_zone_for_secondary_cpu());
    set_svc_stack(new_zone_for_secondary_cpu());
    set_irq_stack(new_zone_for_secondary_cpu());
}

void gic_setup()
{
    gicv2_install();
}

void gic_setup_secondary_cpu()
{
    gicv2_install_secondary_cpu();
}

void irq_set_gic_desc(gic_descritptor_t gic_desc)
{
    gic_descriptor = gic_desc;
}

void undefined_handler(trapframe_t* tf)
{
    system_disable_interrupts();
#ifdef FPU_ENABLED
    if (!RUNNING_THREAD) {
        goto undefined_h;
    }

    if (fpu_is_avail()) {
        goto undefined_h;
    }

    fpu_make_avail();

    if (RUNNING_THREAD->tid == THIS_CPU->fpu_for_pid) {
        return;
    }

    if (THIS_CPU->fpu_for_thread && THIS_CPU->fpu_for_thread->tid == THIS_CPU->fpu_for_pid) {
        fpu_save(THIS_CPU->fpu_for_thread->fpu_state);
    }

    fpu_restore(RUNNING_THREAD->fpu_state);
    THIS_CPU->fpu_for_thread = RUNNING_THREAD;
    THIS_CPU->fpu_for_pid = RUNNING_THREAD->tid;
    system_enable_interrupts_only_counter();
    return;
#endif // FPU_ENABLED

undefined_h:
    if (THIS_CPU->current_state == CPU_IN_USERLAND && RUNNING_THREAD) {
        dump_and_kill(RUNNING_THREAD->process);
    } else {
        log("undefined_handler address; ip: %x, running tid: %d", tf->user_ip, RUNNING_THREAD->tid);
        ASSERT(false);
    }
    system_enable_interrupts_only_counter();
}

void svc_handler(trapframe_t* tf)
{
    sys_handler(tf);
}

void prefetch_abort_handler()
{
    uint32_t val;
    asm volatile("mov %0, lr"
                 : "=r"(val)
                 :);
    if (THIS_CPU->current_state == CPU_IN_USERLAND && RUNNING_THREAD) {
        log("[cpu %d] prefetch_abort_handler pid: %d", system_cpu_id(), RUNNING_THREAD->tid);
        dump_and_kill(RUNNING_THREAD->process);
    } else {
        log("[cpu %d] prefetch_abort_handler address : %x", system_cpu_id(), val);
        system_stop();
    }
}

void data_abort_handler(trapframe_t* tf)
{
    system_disable_interrupts();
    int trap_state = THIS_CPU->current_state;

    cpu_enter_kernel_space();
    uint32_t fault_addr = read_far();
    uint32_t info = read_dfsr();
    uint32_t is_pl0 = read_spsr() & 0xf; // See CPSR M field values
    info |= ((is_pl0 != 0) << 31); // Set the 31bit as type
    int res = vmm_page_fault_handler(info, fault_addr);
    if (res == SHOULD_CRASH) {
        if (THIS_CPU->current_state == CPU_IN_KERNEL || !RUNNING_THREAD) {
            snprintf(err_buf, ERR_BUF_SIZE, "Kernel trap at %x, data_abort_handler", tf->user_ip);
            kpanic_tf(err_buf, tf);
        } else {
            log_warn("Crash: pf err %d at %x: %d pid, %x eip\n", info, fault_addr, RUNNING_THREAD->tid, tf->user_ip);
            dump_and_kill(RUNNING_THREAD->process);
        }
    }
    cpu_leave_kernel_space();
    system_enable_interrupts_only_counter();
}

/**
 * IRQ
 */

static void _irq_empty_handler()
{
    return;
}

static void init_irq_handlers()
{
    for (int i = 0; i < IRQ_HANDLERS_MAX; i++) {
        _irq_handlers[i] = _irq_empty_handler;
    }
}

static inline void _irq_redirect(irq_line_t line)
{
    _irq_handlers[line]();
}

void irq_handler(trapframe_t* tf)
{
    system_disable_interrupts();
    cpu_enter_kernel_space();
    uint32_t int_disc = gic_descriptor.interrupt_descriptor();
    /* We end the interrupt before handle it, since we can
       call sched() and not return here. */
    gic_descriptor.end_interrupt(int_disc);
    _irq_redirect(int_disc & 0x1ff);
    cpu_leave_kernel_space();
    system_enable_interrupts_only_counter();
}

void fast_irq_handler()
{
    log("fast_irq_handler");
    ASSERT(false);
}

void irq_register_handler(irq_line_t line, irq_priority_t prior, irq_type_t type, irq_handler_t func, int cpu_mask)
{
    _irq_handlers[line] = func;
    gic_descriptor.enable_irq(line, prior, type, cpu_mask);
}