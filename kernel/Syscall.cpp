/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <kernel/api/Syscall.h>
#include <kernel/arch/x86/Interrupts.h>
#include <kernel/arch/x86/TrapFrame.h>
#include <kernel/memory/MemoryManager.h>
#include <kernel/Panic.h>
#include <kernel/PerformanceManager.h>
#include <kernel/Process.h>
#include <kernel/Sections.h>
#include <kernel/ThreadTracer.h>

namespace Kernel {

extern "C" void syscall_handler(TrapFrame*) __attribute__((used));
extern "C" void syscall_asm_entry();

NEVER_INLINE NAKED void syscall_asm_entry()
{
    
#if ARCH(I386)
    asm(
        "    pushl $0x0\n"
        "    pusha\n"
        "    pushl %ds\n"
        "    pushl %es\n"
        "    pushl %fs\n"
        "    pushl %gs\n"
        "    pushl %ss\n"
        "    mov $" __STRINGIFY(GDT_SELECTOR_DATA0) ", %ax\n"
        "    mov %ax, %ds\n"
        "    mov %ax, %es\n"
        "    mov $" __STRINGIFY(GDT_SELECTOR_PROC) ", %ax\n"
        "    mov %ax, %gs\n"
        "    cld\n"
        "    xor %esi, %esi\n"
        "    xor %edi, %edi\n"
        "    pushl %esp \n" 
        "    subl $" __STRINGIFY(TRAP_FRAME_SIZE - 4) ", %esp \n"
        "    movl %esp, %ebx \n"
        "    pushl %ebx \n" 
        "    call enter_trap_no_irq \n"
        "    movl %ebx, 0(%esp) \n"
        "    call syscall_handler \n"
        "    movl %ebx, 0(%esp) \n" 
        "    jmp common_trap_exit \n");
#elif ARCH(X86_64)
    asm(
        "    pushq $0x0\n"
        "    pushq %r15\n"
        "    pushq %r14\n"
        "    pushq %r13\n"
        "    pushq %r12\n"
        "    pushq %r11\n"
        "    pushq %r10\n"
        "    pushq %r9\n"
        "    pushq %r8\n"
        "    pushq %rax\n"
        "    pushq %rcx\n"
        "    pushq %rdx\n"
        "    pushq %rbx\n"
        "    pushq %rsp\n"
        "    pushq %rbp\n"
        "    pushq %rsi\n"
        "    pushq %rdi\n"
        "    pushq %rsp \n" /* set TrapFrame::regs */
        "    subq $" __STRINGIFY(TRAP_FRAME_SIZE - 8) ", %rsp \n"
        "    movq %rsp, %rdi \n"
        "    cld\n"
        "    call enter_trap_no_irq \n"
        "    movq %rsp, %rdi \n"
        "    call syscall_handler\n"
        "    jmp common_trap_exit \n");
#endif
    
}

namespace Syscall {

static KResultOr<FlatPtr> handle(RegisterState&, FlatPtr function, FlatPtr arg1, FlatPtr arg2, FlatPtr arg3, FlatPtr arg4);

UNMAP_AFTER_INIT void initialize()
{
    register_user_callable_interrupt_handler(syscall_vector, syscall_asm_entry);
}

#pragma GCC diagnostic ignored "-Wcast-function-type"
typedef KResultOr<FlatPtr> (Process::*Handler)(FlatPtr, FlatPtr, FlatPtr, FlatPtr);
typedef KResultOr<FlatPtr> (Process::*HandlerWithRegisterState)(RegisterState&);
struct HandlerMetadata {
    Handler handler;
    NeedsBigProcessLock needs_lock;
};

#define __ENUMERATE_SYSCALL(sys_call, needs_lock) { reinterpret_cast<Handler>(&Process::sys$##sys_call), needs_lock },
static const HandlerMetadata s_syscall_table[] = {
    ENUMERATE_SYSCALLS(__ENUMERATE_SYSCALL)
};
#undef __ENUMERATE_SYSCALL

KResultOr<FlatPtr> handle(RegisterState& regs, FlatPtr function, FlatPtr arg1, FlatPtr arg2, FlatPtr arg3, FlatPtr arg4)
{
    VERIFY_INTERRUPTS_ENABLED();
    auto current_thread = Thread::current();
    auto& process = current_thread->process();
    current_thread->did_syscall();

    PerformanceManager::add_syscall_event(*current_thread, regs);

    if (function >= Function::__Count) {
        dbgln("Unknown syscall {} requested ({:p}, {:p}, {:p}, {:p})", function, arg1, arg2, arg3, arg4);
        return ENOSYS;
    }

    const auto syscall_metadata = s_syscall_table[function];
    if (syscall_metadata.handler == nullptr) {
        dbgln("Null syscall {} requested, you probably need to rebuild this program!", function);
        return ENOSYS;
    }

    MutexLocker mutex_locker;
    const auto needs_big_lock = syscall_metadata.needs_lock == NeedsBigProcessLock::Yes;
    if (needs_big_lock) {
        mutex_locker.attach_and_lock(process.big_lock());
    };

    if (function == SC_exit || function == SC_exit_thread) {
        
        

        if (auto* tracer = process.tracer(); tracer && tracer->is_tracing_syscalls()) {
            regs.set_return_reg(0);
            tracer->set_trace_syscalls(false);
            process.tracer_trap(*current_thread, regs); 
        }

        switch (function) {
        case SC_exit:
            process.sys$exit(arg1);
            break;
        case SC_exit_thread:
            process.sys$exit_thread(arg1, arg2, arg3);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }

    KResultOr<FlatPtr> result { FlatPtr(nullptr) };
    if (function == SC_fork || function == SC_sigreturn) {

        auto handler = (HandlerWithRegisterState)syscall_metadata.handler;
        result = (process.*(handler))(regs);
    } else {
        result = (process.*(syscall_metadata.handler))(arg1, arg2, arg3, arg4);
    }

    return result;
}

}

NEVER_INLINE void syscall_handler(TrapFrame* trap)
{
    auto& regs = *trap->regs;
    auto current_thread = Thread::current();
    VERIFY(current_thread->previous_mode() == Thread::PreviousMode::UserMode);
    auto& process = current_thread->process();
    if (process.is_dying()) {
        
        current_thread->die_if_needed();
        return;
    }

    if (auto tracer = process.tracer(); tracer && tracer->is_tracing_syscalls()) {
        tracer->set_trace_syscalls(false);
        process.tracer_trap(*current_thread, regs); // this triggers SIGTRAP and stops the thread!
    }

    current_thread->yield_if_stopped();

    clac();

    u32 lsw;
    u32 msw;
    read_tsc(lsw, msw);

    auto* ptr = (char*)__builtin_alloca(lsw & 0xff);
    asm volatile(""
                 : "=m"(*ptr));

    static constexpr FlatPtr iopl_mask = 3u << 12;

    FlatPtr flags = regs.flags();
    if ((flags & (iopl_mask)) != 0) {
        PANIC("Syscall from process with IOPL != 0");
    }

    MM.validate_syscall_preconditions(process.address_space(), regs);

    FlatPtr function;
    FlatPtr arg1;
    FlatPtr arg2;
    FlatPtr arg3;
    FlatPtr arg4;
    regs.capture_syscall_params(function, arg1, arg2, arg3, arg4);

    auto result = Syscall::handle(regs, function, arg1, arg2, arg3, arg4);

    if (result.is_error()) {
        regs.set_return_reg(result.error());
    } else {
        regs.set_return_reg(result.value());
    }

    if (auto tracer = process.tracer(); tracer && tracer->is_tracing_syscalls()) {
        tracer->set_trace_syscalls(false);
        process.tracer_trap(*current_thread, regs); 
    }

    current_thread->yield_if_stopped();

    current_thread->check_dispatch_pending_signal();

    VERIFY(current_thread->previous_mode() == Thread::PreviousMode::UserMode);

    current_thread->die_if_needed();

    VERIFY(!g_scheduler_lock.own_lock());
}

}