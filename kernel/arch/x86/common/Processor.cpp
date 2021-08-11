/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <base/Format.h>
#include <base/StdLibExtras.h>
#include <base/String.h>
#include <base/Types.h>
#include <kernel/interrupts/APIC.h>
#include <kernel/memory/ProcessPagingScope.h>
#include <kernel/Process.h>
#include <kernel/Sections.h>
#include <kernel/StdLib.h>
#include <kernel/Thread.h>
#include <kernel/arch/x86/CPUID.h>
#include <kernel/arch/x86/Interrupts.h>
#include <kernel/arch/x86/MSR.h>
#include <kernel/arch/x86/Processor.h>
#include <kernel/arch/x86/ProcessorInfo.h>
#include <kernel/arch/x86/SafeMem.h>
#include <kernel/arch/x86/ScopedCritical.h>
#include <kernel/arch/x86/TrapFrame.h>

namespace Kernel {

READONLY_AFTER_INIT FPUState Processor::s_clean_fpu_state;

READONLY_AFTER_INIT static ProcessorContainer s_processors {};
READONLY_AFTER_INIT Atomic<u32> Processor::g_total_processors;
READONLY_AFTER_INIT static volatile bool s_smp_enabled;

static Atomic<ProcessorMessage*> s_message_pool;
Atomic<u32> Processor::s_idle_cpu_mask { 0 };

extern "C" void context_first_init(Thread* from_thread, Thread* to_thread, TrapFrame* trap) __attribute__((used));
extern "C" void enter_thread_context(Thread* from_thread, Thread* to_thread) __attribute__((used));
extern "C" FlatPtr do_init_context(Thread* thread, u32 flags) __attribute__((used));

bool Processor::is_smp_enabled()
{
    return s_smp_enabled;
}

UNMAP_AFTER_INIT static void sse_init()
{
    write_cr0((read_cr0() & 0xfffffffbu) | 0x2);
    write_cr4(read_cr4() | 0x600);
}

void exit_kernel_thread(void)
{
    Thread::current()->exit();
}

UNMAP_AFTER_INIT void Processor::cpu_detect()
{
    
    auto set_feature =
        [&](CPUFeature f) {
            m_features = static_cast<CPUFeature>(static_cast<u32>(m_features) | static_cast<u32>(f));
        };
    m_features = static_cast<CPUFeature>(0);

    CPUID processor_info(0x1);
    if (processor_info.edx() & (1 << 4))
        set_feature(CPUFeature::TSC);
    if (processor_info.edx() & (1 << 6))
        set_feature(CPUFeature::PAE);
    if (processor_info.edx() & (1 << 13))
        set_feature(CPUFeature::PGE);
    if (processor_info.edx() & (1 << 23))
        set_feature(CPUFeature::MMX);
    if (processor_info.edx() & (1 << 24))
        set_feature(CPUFeature::FXSR);
    if (processor_info.edx() & (1 << 25))
        set_feature(CPUFeature::SSE);
    if (processor_info.edx() & (1 << 26))
        set_feature(CPUFeature::SSE2);
    if (processor_info.ecx() & (1 << 0))
        set_feature(CPUFeature::SSE3);
    if (processor_info.ecx() & (1 << 9))
        set_feature(CPUFeature::SSSE3);
    if (processor_info.ecx() & (1 << 19))
        set_feature(CPUFeature::SSE4_1);
    if (processor_info.ecx() & (1 << 20))
        set_feature(CPUFeature::SSE4_2);
    if (processor_info.ecx() & (1 << 26))
        set_feature(CPUFeature::XSAVE);
    if (processor_info.ecx() & (1 << 28))
        set_feature(CPUFeature::AVX);
    if (processor_info.ecx() & (1 << 30))
        set_feature(CPUFeature::RDRAND);
    if (processor_info.ecx() & (1u << 31))
        set_feature(CPUFeature::HYPERVISOR);
    if (processor_info.edx() & (1 << 11)) {
        u32 stepping = processor_info.eax() & 0xf;
        u32 model = (processor_info.eax() >> 4) & 0xf;
        u32 family = (processor_info.eax() >> 8) & 0xf;
        if (!(family == 6 && model < 3 && stepping < 3))
            set_feature(CPUFeature::SEP);
        if ((family == 6 && model >= 3) || (family == 0xf && model >= 0xe))
            set_feature(CPUFeature::CONSTANT_TSC);
    }

    u32 max_extended_leaf = CPUID(0x80000000).eax();

    if (max_extended_leaf >= 0x80000001) {
        CPUID extended_processor_info(0x80000001);
        if (extended_processor_info.edx() & (1 << 20))
            set_feature(CPUFeature::NX);
        if (extended_processor_info.edx() & (1 << 27))
            set_feature(CPUFeature::RDTSCP);
        if (extended_processor_info.edx() & (1 << 29))
            set_feature(CPUFeature::LM);
        if (extended_processor_info.edx() & (1 << 11)) {

            set_feature(CPUFeature::SYSCALL);
        }
    }

    if (max_extended_leaf >= 0x80000007) {
        CPUID cpuid(0x80000007);
        if (cpuid.edx() & (1 << 8)) {
            set_feature(CPUFeature::CONSTANT_TSC);
            set_feature(CPUFeature::NONSTOP_TSC);
        }
    }

    if (max_extended_leaf >= 0x80000008) {

        CPUID cpuid(0x80000008);
        m_physical_address_bit_width = cpuid.eax() & 0xff;
    } else {

        m_physical_address_bit_width = has_feature(CPUFeature::PAE) ? 36 : 32;
    }

    CPUID extended_features(0x7);
    if (extended_features.ebx() & (1 << 20))
        set_feature(CPUFeature::SMAP);
    if (extended_features.ebx() & (1 << 7))
        set_feature(CPUFeature::SMEP);
    if (extended_features.ecx() & (1 << 2))
        set_feature(CPUFeature::UMIP);
    if (extended_features.ebx() & (1 << 18))
        set_feature(CPUFeature::RDSEED);
}

UNMAP_AFTER_INIT void Processor::cpu_setup()
{

    cpu_detect();

    if (has_feature(CPUFeature::SSE)) {

        VERIFY(has_feature(CPUFeature::FXSR));
        sse_init();
    }

    write_cr0(read_cr0() | 0x00010000);

    if (has_feature(CPUFeature::PGE)) {

        write_cr4(read_cr4() | 0x80);
    }

    if (has_feature(CPUFeature::NX)) {
        asm volatile(
            "movl $0xc0000080, %ecx\n"
            "rdmsr\n"
            "orl $0x800, %eax\n"
            "wrmsr\n");
    }

    if (has_feature(CPUFeature::SMEP)) {

        write_cr4(read_cr4() | 0x100000);
    }

    if (has_feature(CPUFeature::SMAP)) {

        write_cr4(read_cr4() | 0x200000);
    }

    if (has_feature(CPUFeature::UMIP)) {
        write_cr4(read_cr4() | 0x800);
    }

    if (has_feature(CPUFeature::TSC)) {
        write_cr4(read_cr4() | 0x4);
    }

    if (has_feature(CPUFeature::XSAVE)) {

        write_cr4(read_cr4() | 0x40000);

        write_xcr0(0x1);

        if (has_feature(CPUFeature::AVX)) {
            write_xcr0(read_xcr0() | 0x7);
        }
    }
}

String Processor::features_string() const
{
    StringBuilder builder;
    auto feature_to_str =
        [](CPUFeature f) -> const char* {
        switch (f) {
        case CPUFeature::NX:
            return "nx";
        case CPUFeature::PAE:
            return "pae";
        case CPUFeature::PGE:
            return "pge";
        case CPUFeature::RDRAND:
            return "rdrand";
        case CPUFeature::RDSEED:
            return "rdseed";
        case CPUFeature::SMAP:
            return "smap";
        case CPUFeature::SMEP:
            return "smep";
        case CPUFeature::SSE:
            return "sse";
        case CPUFeature::TSC:
            return "tsc";
        case CPUFeature::RDTSCP:
            return "rdtscp";
        case CPUFeature::CONSTANT_TSC:
            return "constant_tsc";
        case CPUFeature::NONSTOP_TSC:
            return "nonstop_tsc";
        case CPUFeature::UMIP:
            return "umip";
        case CPUFeature::SEP:
            return "sep";
        case CPUFeature::SYSCALL:
            return "syscall";
        case CPUFeature::MMX:
            return "mmx";
        case CPUFeature::FXSR:
            return "fxsr";
        case CPUFeature::SSE2:
            return "sse2";
        case CPUFeature::SSE3:
            return "sse3";
        case CPUFeature::SSSE3:
            return "ssse3";
        case CPUFeature::SSE4_1:
            return "sse4.1";
        case CPUFeature::SSE4_2:
            return "sse4.2";
        case CPUFeature::XSAVE:
            return "xsave";
        case CPUFeature::AVX:
            return "avx";
        case CPUFeature::LM:
            return "lm";
        case CPUFeature::HYPERVISOR:
            return "hypervisor";

        }

        return "???";
    };
    bool first = true;
    for (u32 flag = 1; flag != 0; flag <<= 1) {
        if ((static_cast<u32>(m_features) & flag) != 0) {
            if (first)
                first = false;
            else
                builder.append(' ');
            auto str = feature_to_str(static_cast<CPUFeature>(flag));
            builder.append(str, strlen(str));
        }
    }
    return builder.build();
}

UNMAP_AFTER_INIT void Processor::early_initialize(u32 cpu)
{
    m_self = this;

    m_cpu = cpu;
    m_in_irq = 0;
    m_in_critical = 0;

    m_invoke_scheduler_async = false;
    m_scheduler_initialized = false;

    m_message_queue = nullptr;
    m_idle_thread = nullptr;
    m_current_thread = nullptr;
    m_info = nullptr;

    m_halt_requested = false;
    if (cpu == 0) {
        s_smp_enabled = false;
        g_total_processors.store(1u, Base::MemoryOrder::memory_order_release);
    } else {
        g_total_processors.fetch_add(1u, Base::MemoryOrder::memory_order_acq_rel);
    }

    deferred_call_pool_init();

    cpu_setup();
    gdt_init();

    VERIFY(is_initialized());   
    VERIFY(&current() == this); 
}

UNMAP_AFTER_INIT void Processor::initialize(u32 cpu)
{
    VERIFY(m_self == this);
    VERIFY(&current() == this); 

    dmesgln("CPU[{}]: Supported features: {}", id(), features_string());
    if (!has_feature(CPUFeature::RDRAND))
        dmesgln("CPU[{}]: No RDRAND support detected, randomness will be poor", id());
    dmesgln("CPU[{}]: Physical address bit width: {}", id(), m_physical_address_bit_width);

    if (cpu == 0)
        idt_init();
    else
        flush_idt();

    if (cpu == 0) {
        VERIFY((FlatPtr(&s_clean_fpu_state) & 0xF) == 0);
        asm volatile("fninit");
        if (has_feature(CPUFeature::FXSR))
            asm volatile("fxsave %0"
                         : "=m"(s_clean_fpu_state));
        else
            asm volatile("fnsave %0"
                         : "=m"(s_clean_fpu_state));

        if (has_feature(CPUFeature::HYPERVISOR))
            detect_hypervisor();
    }

    m_info = new ProcessorInfo(*this);

    {

        VERIFY(cpu < s_processors.size());
        s_processors[cpu] = this;
    }
}

UNMAP_AFTER_INIT void Processor::detect_hypervisor()
{
    CPUID hypervisor_leaf_range(0x40000000);


    alignas(sizeof(u32)) char hypervisor_signature_buffer[13];
    *reinterpret_cast<u32*>(hypervisor_signature_buffer) = hypervisor_leaf_range.ebx();
    *reinterpret_cast<u32*>(hypervisor_signature_buffer + 4) = hypervisor_leaf_range.ecx();
    *reinterpret_cast<u32*>(hypervisor_signature_buffer + 8) = hypervisor_leaf_range.edx();
    hypervisor_signature_buffer[12] = '\0';
    StringView hypervisor_signature(hypervisor_signature_buffer);

    dmesgln("CPU[{}]: CPUID hypervisor signature '{}' ({:#x} {:#x} {:#x}), max leaf {:#x}", id(), hypervisor_signature, hypervisor_leaf_range.ebx(), hypervisor_leaf_range.ecx(), hypervisor_leaf_range.edx(), hypervisor_leaf_range.eax());

    if (hypervisor_signature == "Microsoft Hv"sv)
        detect_hypervisor_hyperv(hypervisor_leaf_range);
}

UNMAP_AFTER_INIT void Processor::detect_hypervisor_hyperv(CPUID const& hypervisor_leaf_range)
{
    if (hypervisor_leaf_range.eax() < 0x40000001)
        return;

    CPUID hypervisor_interface(0x40000001);

    alignas(sizeof(u32)) char interface_signature_buffer[5];
    *reinterpret_cast<u32*>(interface_signature_buffer) = hypervisor_interface.eax();
    interface_signature_buffer[4] = '\0';
    StringView hyperv_interface_signature(interface_signature_buffer);

    dmesgln("CPU[{}]: Hyper-V interface signature '{}' ({:#x})", id(), hyperv_interface_signature, hypervisor_interface.eax());

    if (hypervisor_leaf_range.eax() < 0x40000001)
        return;

    CPUID hypervisor_sysid(0x40000002);
    dmesgln("CPU[{}]: Hyper-V system identity {}.{}, build number {}", id(), hypervisor_sysid.ebx() >> 16, hypervisor_sysid.ebx() & 0xFFFF, hypervisor_sysid.eax());

    if (hypervisor_leaf_range.eax() < 0x40000005 || hyperv_interface_signature != "Hv#1"sv)
        return;

    dmesgln("CPU[{}]: Hyper-V hypervisor detected", id());

}

void Processor::write_raw_gdt_entry(u16 selector, u32 low, u32 high)
{
    u16 i = (selector & 0xfffc) >> 3;
    u32 prev_gdt_length = m_gdt_length;

    if (i >= m_gdt_length) {
        m_gdt_length = i + 1;
        VERIFY(m_gdt_length <= sizeof(m_gdt) / sizeof(m_gdt[0]));
        m_gdtr.limit = (m_gdt_length + 1) * 8 - 1;
    }
    m_gdt[i].low = low;
    m_gdt[i].high = high;

    while (i < prev_gdt_length) {
        m_gdt[i].low = 0;
        m_gdt[i].high = 0;
        i++;
    }
}

void Processor::write_gdt_entry(u16 selector, Descriptor& descriptor)
{
    write_raw_gdt_entry(selector, descriptor.low, descriptor.high);
}

Descriptor& Processor::get_gdt_entry(u16 selector)
{
    u16 i = (selector & 0xfffc) >> 3;
    return *(Descriptor*)(&m_gdt[i]);
}

void Processor::flush_gdt()
{
    m_gdtr.address = m_gdt;
    m_gdtr.limit = (m_gdt_length * 8) - 1;
    asm volatile("lgdt %0" ::"m"(m_gdtr)
                 : "memory");
}

const DescriptorTablePointer& Processor::get_gdtr()
{
    return m_gdtr;
}

Vector<FlatPtr> Processor::capture_stack_trace(Thread& thread, size_t max_frames)
{
    FlatPtr frame_ptr = 0, ip = 0;
    Vector<FlatPtr, 32> stack_trace;

    auto walk_stack = [&](FlatPtr stack_ptr) {
        static constexpr size_t max_stack_frames = 4096;
        stack_trace.append(ip);
        size_t count = 1;
        while (stack_ptr && stack_trace.size() < max_stack_frames) {
            FlatPtr retaddr;

            count++;
            if (max_frames != 0 && count > max_frames)
                break;

            if (Memory::is_user_range(VirtualAddress(stack_ptr), sizeof(FlatPtr) * 2)) {
                if (!copy_from_user(&retaddr, &((FlatPtr*)stack_ptr)[1]) || !retaddr)
                    break;
                stack_trace.append(retaddr);
                if (!copy_from_user(&stack_ptr, (FlatPtr*)stack_ptr))
                    break;
            } else {
                void* fault_at;
                if (!safe_memcpy(&retaddr, &((FlatPtr*)stack_ptr)[1], sizeof(FlatPtr), fault_at) || !retaddr)
                    break;
                stack_trace.append(retaddr);
                if (!safe_memcpy(&stack_ptr, (FlatPtr*)stack_ptr, sizeof(FlatPtr), fault_at))
                    break;
            }
        }
    };
    auto capture_current_thread = [&]() {
        frame_ptr = (FlatPtr)__builtin_frame_address(0);
        ip = (FlatPtr)__builtin_return_address(0);

        walk_stack(frame_ptr);
    };

    ScopedSpinLock lock(g_scheduler_lock);
    if (&thread == Processor::current_thread()) {
        VERIFY(thread.state() == Thread::Running);

        lock.unlock();
        capture_current_thread();
    } else if (thread.is_active()) {
        VERIFY(thread.cpu() != Processor::id());

        auto& proc = Processor::current();
        smp_unicast(
            thread.cpu(),
            [&]() {
                dbgln("CPU[{}] getting stack for cpu #{}", Processor::id(), proc.get_id());
                ProcessPagingScope paging_scope(thread.process());
                VERIFY(&Processor::current() != &proc);
                VERIFY(&thread == Processor::current_thread());

                capture_current_thread();
            },
            false);
    } else {
        switch (thread.state()) {
        case Thread::Running:
            VERIFY_NOT_REACHED(); 
        case Thread::Runnable:
        case Thread::Stopped:
        case Thread::Blocked:
        case Thread::Dying:
        case Thread::Dead: {

            ProcessPagingScope paging_scope(thread.process());
            auto& regs = thread.regs();
            FlatPtr* stack_top = reinterpret_cast<FlatPtr*>(regs.sp());
            if (Memory::is_user_range(VirtualAddress(stack_top), sizeof(FlatPtr))) {
                if (!copy_from_user(&frame_ptr, &((FlatPtr*)stack_top)[0]))
                    frame_ptr = 0;
            } else {
                void* fault_at;
                if (!safe_memcpy(&frame_ptr, &((FlatPtr*)stack_top)[0], sizeof(FlatPtr), fault_at))
                    frame_ptr = 0;
            }

            ip = regs.ip();

            lock.unlock();
            walk_stack(frame_ptr);
            break;
        }
        default:
            dbgln("Cannot capture stack trace for thread {} in state {}", thread, thread.state_string());
            break;
        }
    }
    return stack_trace;
}

ProcessorContainer& Processor::processors()
{
    return s_processors;
}

void Processor::enter_trap(TrapFrame& trap, bool raise_irq)
{
    VERIFY_INTERRUPTS_DISABLED();
    VERIFY(&Processor::current() == this);
    trap.prev_irq_level = m_in_irq;
    if (raise_irq)
        m_in_irq++;
    auto* current_thread = Processor::current_thread();
    if (current_thread) {
        auto& current_trap = current_thread->current_trap();
        trap.next_trap = current_trap;
        current_trap = &trap;
        auto new_previous_mode = ((trap.regs->cs & 3) != 0) ? Thread::PreviousMode::UserMode : Thread::PreviousMode::KernelMode;
        if (current_thread->set_previous_mode(new_previous_mode) && trap.prev_irq_level == 0) {
            current_thread->update_time_scheduled(Scheduler::current_time(), new_previous_mode == Thread::PreviousMode::KernelMode, false);
        }
    } else {
        trap.next_trap = nullptr;
    }
}

void Processor::exit_trap(TrapFrame& trap)
{
    VERIFY_INTERRUPTS_DISABLED();
    VERIFY(&Processor::current() == this);

    m_in_critical = m_in_critical + 1;

    VERIFY(m_in_irq >= trap.prev_irq_level);
    m_in_irq = trap.prev_irq_level;

    if (s_smp_enabled)
        smp_process_pending_messages();

    deferred_call_execute_pending();

    auto* current_thread = Processor::current_thread();
    if (current_thread) {
        auto& current_trap = current_thread->current_trap();
        current_trap = trap.next_trap;
        Thread::PreviousMode new_previous_mode;
        if (current_trap) {
            VERIFY(current_trap->regs);

            new_previous_mode = ((current_trap->regs->cs & 3) != 0) ? Thread::PreviousMode::UserMode : Thread::PreviousMode::KernelMode;
        } else {

            new_previous_mode = Thread::PreviousMode::KernelMode;
        }

        if (current_thread->set_previous_mode(new_previous_mode))
            current_thread->update_time_scheduled(Scheduler::current_time(), true, false);
    }

    VERIFY_INTERRUPTS_DISABLED();

    m_in_critical = m_in_critical - 1;
    if (!m_in_irq && !m_in_critical)
        check_invoke_scheduler();
}

void Processor::check_invoke_scheduler()
{
    InterruptDisabler disabler;
    VERIFY(!m_in_irq);
    VERIFY(!m_in_critical);
    VERIFY(&Processor::current() == this);
    if (m_invoke_scheduler_async && m_scheduler_initialized) {
        m_invoke_scheduler_async = false;
        Scheduler::invoke_async();
    }
}

void Processor::flush_tlb_local(VirtualAddress vaddr, size_t page_count)
{
    auto ptr = vaddr.as_ptr();
    while (page_count > 0) {

        asm volatile("invlpg %0"
             :
             : "m"(*ptr)
             : "memory");

        ptr += PAGE_SIZE;
        page_count--;
    }
}

void Processor::flush_tlb(Memory::PageDirectory const* page_directory, VirtualAddress vaddr, size_t page_count)
{
    if (s_smp_enabled && (!Memory::is_user_address(vaddr) || Process::current()->thread_count() > 1))
        smp_broadcast_flush_tlb(page_directory, vaddr, page_count);
    else
        flush_tlb_local(vaddr, page_count);
}

void Processor::smp_return_to_pool(ProcessorMessage& msg)
{
    ProcessorMessage* next = nullptr;
    for (;;) {
        msg.next = next;
        if (s_message_pool.compare_exchange_strong(next, &msg, Base::MemoryOrder::memory_order_acq_rel))
            break;
        Processor::pause();
    }
}

ProcessorMessage& Processor::smp_get_from_pool()
{
    ProcessorMessage* msg;

    for (;;) {
        msg = s_message_pool.load(Base::MemoryOrder::memory_order_consume);
        if (!msg) {
            if (!Processor::current().smp_process_pending_messages()) {
                Processor::pause();
            }
            continue;
        }

        if (s_message_pool.compare_exchange_strong(msg, msg->next, Base::MemoryOrder::memory_order_acq_rel)) {

            break;
        }
    }

    VERIFY(msg != nullptr);
    return *msg;
}

u32 Processor::smp_wake_n_idle_processors(u32 wake_count)
{
    VERIFY_INTERRUPTS_DISABLED();
    VERIFY(wake_count > 0);
    if (!s_smp_enabled)
        return 0;

    if (wake_count >= Processor::count()) {
        wake_count = Processor::count() - 1;
        VERIFY(wake_count > 0);
    }

    u32 current_id = Processor::current().id();

    u32 did_wake_count = 0;
    auto& apic = APIC::the();
    while (did_wake_count < wake_count) {

        u32 idle_mask = s_idle_cpu_mask.load(Base::MemoryOrder::memory_order_relaxed) & ~(1u << current_id);
        u32 idle_count = __builtin_popcountl(idle_mask);
        if (idle_count == 0)
            break; 

        u32 found_mask = 0;
        for (u32 i = 0; i < idle_count; i++) {
            u32 cpu = __builtin_ffsl(idle_mask) - 1;
            idle_mask &= ~(1u << cpu);
            found_mask |= 1u << cpu;
        }

        idle_mask = s_idle_cpu_mask.fetch_and(~found_mask, Base::MemoryOrder::memory_order_acq_rel) & found_mask;
        if (idle_mask == 0)
            continue; 
        idle_count = __builtin_popcountl(idle_mask);
        for (u32 i = 0; i < idle_count; i++) {
            u32 cpu = __builtin_ffsl(idle_mask) - 1;
            idle_mask &= ~(1u << cpu);

            apic.send_ipi(cpu);
            did_wake_count++;
        }
    }
    return did_wake_count;
}

UNMAP_AFTER_INIT void Processor::smp_enable()
{
    size_t msg_pool_size = Processor::count() * 100u;
    size_t msg_entries_cnt = Processor::count();

    auto msgs = new ProcessorMessage[msg_pool_size];
    auto msg_entries = new ProcessorMessageEntry[msg_pool_size * msg_entries_cnt];
    size_t msg_entry_i = 0;
    for (size_t i = 0; i < msg_pool_size; i++, msg_entry_i += msg_entries_cnt) {
        auto& msg = msgs[i];
        msg.next = i < msg_pool_size - 1 ? &msgs[i + 1] : nullptr;
        msg.per_proc_entries = &msg_entries[msg_entry_i];
        for (size_t k = 0; k < msg_entries_cnt; k++)
            msg_entries[msg_entry_i + k].msg = &msg;
    }

    s_message_pool.store(&msgs[0], Base::MemoryOrder::memory_order_release);

    s_smp_enabled = true;
}

void Processor::smp_cleanup_message(ProcessorMessage& msg)
{
    switch (msg.type) {
    case ProcessorMessage::Callback:
        msg.callback_value().~Function();
        break;
    default:
        break;
    }
}

bool Processor::smp_process_pending_messages()
{
    VERIFY(s_smp_enabled);

    bool did_process = false;
    enter_critical();

    if (auto pending_msgs = m_message_queue.exchange(nullptr, Base::MemoryOrder::memory_order_acq_rel)) {

        auto reverse_list =
            [](ProcessorMessageEntry* list) -> ProcessorMessageEntry* {
            ProcessorMessageEntry* rev_list = nullptr;
            while (list) {
                auto next = list->next;
                list->next = rev_list;
                rev_list = list;
                list = next;
            }
            return rev_list;
        };

        pending_msgs = reverse_list(pending_msgs);

        ProcessorMessageEntry* next_msg;
        for (auto cur_msg = pending_msgs; cur_msg; cur_msg = next_msg) {
            next_msg = cur_msg->next;
            auto msg = cur_msg->msg;

            dbgln_if(SMP_DEBUG, "SMP[{}]: Processing message {}", id(), VirtualAddress(msg));

            switch (msg->type) {
            case ProcessorMessage::Callback:
                msg->invoke_callback();
                break;
            case ProcessorMessage::FlushTlb:
                if (Memory::is_user_address(VirtualAddress(msg->flush_tlb.ptr))) {

                    VERIFY(Memory::is_user_range(VirtualAddress(msg->flush_tlb.ptr), msg->flush_tlb.page_count * PAGE_SIZE));
                    if (read_cr3() != msg->flush_tlb.page_directory->cr3()) {
                        
                        dbgln_if(SMP_DEBUG, "SMP[{}]: No need to flush {} pages at {}", id(), msg->flush_tlb.page_count, VirtualAddress(msg->flush_tlb.ptr));
                        break;
                    }
                }
                flush_tlb_local(VirtualAddress(msg->flush_tlb.ptr), msg->flush_tlb.page_count);
                break;
            }

            bool is_async = msg->async; 
            auto prev_refs = msg->refs.fetch_sub(1u, Base::MemoryOrder::memory_order_acq_rel);
            VERIFY(prev_refs != 0);
            if (prev_refs == 1) {

                if (is_async) {
                    smp_cleanup_message(*msg);
                    smp_return_to_pool(*msg);
                }
            }

            if (m_halt_requested.load(Base::MemoryOrder::memory_order_relaxed))
                halt_this();
        }
        did_process = true;
    } else if (m_halt_requested.load(Base::MemoryOrder::memory_order_relaxed)) {
        halt_this();
    }

    leave_critical();
    return did_process;
}

bool Processor::smp_enqueue_message(ProcessorMessage& msg)
{

    auto& msg_entry = msg.per_proc_entries[get_id()];
    VERIFY(msg_entry.msg == &msg);
    ProcessorMessageEntry* next = nullptr;
    for (;;) {
        msg_entry.next = next;
        if (m_message_queue.compare_exchange_strong(next, &msg_entry, Base::MemoryOrder::memory_order_acq_rel))
            break;
        Processor::pause();
    }

    return next == nullptr;
}

void Processor::smp_broadcast_message(ProcessorMessage& msg)
{
    auto& cur_proc = Processor::current();

    dbgln_if(SMP_DEBUG, "SMP[{}]: Broadcast message {} to cpus: {} proc: {}", cur_proc.get_id(), VirtualAddress(&msg), count(), VirtualAddress(&cur_proc));

    msg.refs.store(count() - 1, Base::MemoryOrder::memory_order_release);
    VERIFY(msg.refs > 0);
    bool need_broadcast = false;
    for_each(
        [&](Processor& proc) {
            if (&proc != &cur_proc) {
                if (proc.smp_enqueue_message(msg))
                    need_broadcast = true;
            }
        });

    if (need_broadcast)
        APIC::the().broadcast_ipi();
}

void Processor::smp_broadcast_wait_sync(ProcessorMessage& msg)
{
    auto& cur_proc = Processor::current();
    VERIFY(!msg.async);

    while (msg.refs.load(Base::MemoryOrder::memory_order_consume) != 0) {
        Processor::pause();

        cur_proc.smp_process_pending_messages();
    }

    smp_cleanup_message(msg);
    smp_return_to_pool(msg);
}

void Processor::smp_unicast_message(u32 cpu, ProcessorMessage& msg, bool async)
{
    auto& cur_proc = Processor::current();
    VERIFY(cpu != cur_proc.get_id());
    auto& target_proc = processors()[cpu];
    msg.async = async;

    dbgln_if(SMP_DEBUG, "SMP[{}]: Send message {} to cpu #{} proc: {}", cur_proc.get_id(), VirtualAddress(&msg), cpu, VirtualAddress(&target_proc));

    msg.refs.store(1u, Base::MemoryOrder::memory_order_release);
    if (target_proc->smp_enqueue_message(msg)) {
        APIC::the().send_ipi(cpu);
    }

    if (!async) {

        while (msg.refs.load(Base::MemoryOrder::memory_order_consume) != 0) {
            Processor::pause();

            cur_proc.smp_process_pending_messages();
        }

        smp_cleanup_message(msg);
        smp_return_to_pool(msg);
    }
}

void Processor::smp_unicast(u32 cpu, Function<void()> callback, bool async)
{
    auto& msg = smp_get_from_pool();
    msg.type = ProcessorMessage::Callback;
    new (msg.callback_storage) ProcessorMessage::CallbackFunction(move(callback));
    smp_unicast_message(cpu, msg, async);
}

void Processor::smp_broadcast_flush_tlb(Memory::PageDirectory const* page_directory, VirtualAddress vaddr, size_t page_count)
{
    auto& msg = smp_get_from_pool();
    msg.async = false;
    msg.type = ProcessorMessage::FlushTlb;
    msg.flush_tlb.page_directory = page_directory;
    msg.flush_tlb.ptr = vaddr.as_ptr();
    msg.flush_tlb.page_count = page_count;
    smp_broadcast_message(msg);

    flush_tlb_local(vaddr, page_count);

    smp_broadcast_wait_sync(msg);
}

void Processor::smp_broadcast_halt()
{
    for_each(
        [&](Processor& proc) {
            proc.m_halt_requested.store(true, Base::MemoryOrder::memory_order_release);
        });

    APIC::the().broadcast_ipi();
}

void Processor::Processor::halt()
{
    if (s_smp_enabled)
        smp_broadcast_halt();

    halt_this();
}

UNMAP_AFTER_INIT void Processor::deferred_call_pool_init()
{
    size_t pool_count = sizeof(m_deferred_call_pool) / sizeof(m_deferred_call_pool[0]);
    for (size_t i = 0; i < pool_count; i++) {
        auto& entry = m_deferred_call_pool[i];
        entry.next = i < pool_count - 1 ? &m_deferred_call_pool[i + 1] : nullptr;
        new (entry.handler_storage) DeferredCallEntry::HandlerFunction;
        entry.was_allocated = false;
    }
    m_pending_deferred_calls = nullptr;
    m_free_deferred_call_pool_entry = &m_deferred_call_pool[0];
}

void Processor::deferred_call_return_to_pool(DeferredCallEntry* entry)
{
    VERIFY(m_in_critical);
    VERIFY(!entry->was_allocated);

    entry->handler_value() = {};

    entry->next = m_free_deferred_call_pool_entry;
    m_free_deferred_call_pool_entry = entry;
}

DeferredCallEntry* Processor::deferred_call_get_free()
{
    VERIFY(m_in_critical);

    if (m_free_deferred_call_pool_entry) {

        auto* entry = m_free_deferred_call_pool_entry;
        m_free_deferred_call_pool_entry = entry->next;
        VERIFY(!entry->was_allocated);
        return entry;
    }

    auto* entry = new DeferredCallEntry;
    new (entry->handler_storage) DeferredCallEntry::HandlerFunction;
    entry->was_allocated = true;
    return entry;
}

void Processor::deferred_call_execute_pending()
{
    VERIFY(m_in_critical);

    if (!m_pending_deferred_calls)
        return;
    auto* pending_list = m_pending_deferred_calls;
    m_pending_deferred_calls = nullptr;

    auto reverse_list =
        [](DeferredCallEntry* list) -> DeferredCallEntry* {
        DeferredCallEntry* rev_list = nullptr;
        while (list) {
            auto next = list->next;
            list->next = rev_list;
            rev_list = list;
            list = next;
        }
        return rev_list;
    };
    pending_list = reverse_list(pending_list);

    do {
        pending_list->invoke_handler();

        auto* next = pending_list->next;
        if (pending_list->was_allocated) {
            pending_list->handler_value().~Function();
            delete pending_list;
        } else
            deferred_call_return_to_pool(pending_list);
        pending_list = next;
    } while (pending_list);
}

void Processor::deferred_call_queue_entry(DeferredCallEntry* entry)
{
    VERIFY(m_in_critical);
    entry->next = m_pending_deferred_calls;
    m_pending_deferred_calls = entry;
}

void Processor::deferred_call_queue(Function<void()> callback)
{
    ScopedCritical critical;
    auto& cur_proc = Processor::current();

    auto* entry = cur_proc.deferred_call_get_free();
    entry->handler_value() = move(callback);

    cur_proc.deferred_call_queue_entry(entry);
}

UNMAP_AFTER_INIT void Processor::gdt_init()
{
    m_gdt_length = 0;
    m_gdtr.address = nullptr;
    m_gdtr.limit = 0;

    write_raw_gdt_entry(0x0000, 0x00000000, 0x00000000);
#if ARCH(I386)
    write_raw_gdt_entry(GDT_SELECTOR_CODE0, 0x0000ffff, 0x00cf9a00);
    write_raw_gdt_entry(GDT_SELECTOR_DATA0, 0x0000ffff, 0x00cf9200);
    write_raw_gdt_entry(GDT_SELECTOR_CODE3, 0x0000ffff, 0x00cffa00);
    write_raw_gdt_entry(GDT_SELECTOR_DATA3, 0x0000ffff, 0x00cff200);
#else
    write_raw_gdt_entry(GDT_SELECTOR_CODE0, 0x0000ffff, 0x00af9a00);
    write_raw_gdt_entry(GDT_SELECTOR_CODE3, 0x0000ffff, 0x00affa00);
    write_raw_gdt_entry(GDT_SELECTOR_DATA3, 0x0000ffff, 0x008ff200);
#endif

#if ARCH(I386)
    Descriptor tls_descriptor {};
    tls_descriptor.low = tls_descriptor.high = 0;
    tls_descriptor.dpl = 3;
    tls_descriptor.segment_present = 1;
    tls_descriptor.granularity = 0;
    tls_descriptor.operation_size64 = 0;
    tls_descriptor.operation_size32 = 1;
    tls_descriptor.descriptor_type = 1;
    tls_descriptor.type = 2;
    write_gdt_entry(GDT_SELECTOR_TLS, tls_descriptor); 

    Descriptor gs_descriptor {};
    gs_descriptor.set_base(VirtualAddress { this });
    gs_descriptor.set_limit(sizeof(Processor) - 1);
    gs_descriptor.dpl = 0;
    gs_descriptor.segment_present = 1;
    gs_descriptor.granularity = 0;
    gs_descriptor.operation_size64 = 0;
    gs_descriptor.operation_size32 = 1;
    gs_descriptor.descriptor_type = 1;
    gs_descriptor.type = 2;
    write_gdt_entry(GDT_SELECTOR_PROC, gs_descriptor); 
#endif

    Descriptor tss_descriptor {};
    tss_descriptor.set_base(VirtualAddress { (size_t)&m_tss & 0xffffffff });
    tss_descriptor.set_limit(sizeof(TSS) - 1);
    tss_descriptor.dpl = 0;
    tss_descriptor.segment_present = 1;
    tss_descriptor.granularity = 0;
    tss_descriptor.operation_size64 = 0;
    tss_descriptor.operation_size32 = 1;
    tss_descriptor.descriptor_type = 0;
    tss_descriptor.type = 9;
    write_gdt_entry(GDT_SELECTOR_TSS, tss_descriptor); 

#if ARCH(X86_64)
    Descriptor tss_descriptor_part2 {};
    tss_descriptor_part2.low = (size_t)&m_tss >> 32;
    write_gdt_entry(GDT_SELECTOR_TSS_PART2, tss_descriptor_part2);
#endif

    flush_gdt();
    load_task_register(GDT_SELECTOR_TSS);

#if ARCH(X86_64)
    MSR gs_base(MSR_GS_BASE);
    gs_base.set((u64)this);
#else
    asm volatile(
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%ss\n" ::"a"(GDT_SELECTOR_DATA0)
        : "memory");
    set_gs(GDT_SELECTOR_PROC);
#endif

#if ARCH(I386)

    asm volatile(
        "ljmpl $" __STRINGIFY(GDT_SELECTOR_CODE0) ", $sanity\n"
        "sanity:\n");

#endif
}

extern "C" void context_first_init([[maybe_unused]] Thread* from_thread, [[maybe_unused]] Thread* to_thread, [[maybe_unused]] TrapFrame* trap)
{
    VERIFY(!are_interrupts_enabled());
    VERIFY(is_kernel_mode());

    dbgln_if(CONTEXT_SWITCH_DEBUG, "switch_context <-- from {} {} to {} {} (context_first_init)", VirtualAddress(from_thread), *from_thread, VirtualAddress(to_thread), *to_thread);

    VERIFY(to_thread == Thread::current());

    Scheduler::enter_current(*from_thread, true);

    auto in_critical = to_thread->saved_critical();
    VERIFY(in_critical > 0);
    Processor::current().restore_in_critical(in_critical);

    FlatPtr flags = trap->regs->flags();
    Scheduler::leave_on_first_switch(flags & ~0x200);
}

extern "C" void enter_thread_context(Thread* from_thread, Thread* to_thread)
{
    VERIFY(from_thread == to_thread || from_thread->state() != Thread::Running);
    VERIFY(to_thread->state() == Thread::Running);

    bool has_fxsr = Processor::current().has_feature(CPUFeature::FXSR);
    Processor::set_current_thread(*to_thread);

    auto& from_regs = from_thread->regs();
    auto& to_regs = to_thread->regs();

    if (has_fxsr)
        asm volatile("fxsave %0"
                     : "=m"(from_thread->fpu_state()));
    else
        asm volatile("fnsave %0"
                     : "=m"(from_thread->fpu_state()));

#if ARCH(I386)
    from_regs.fs = get_fs();
    from_regs.gs = get_gs();
    set_fs(to_regs.fs);
    set_gs(to_regs.gs);
#endif

    if (from_thread->process().is_traced())
        read_debug_registers_into(from_thread->debug_register_state());

    if (to_thread->process().is_traced()) {
        write_debug_registers_from(to_thread->debug_register_state());
    } else {
        clear_debug_registers();
    }

    auto& processor = Processor::current();
#if ARCH(I386)
    auto& tls_descriptor = processor.get_gdt_entry(GDT_SELECTOR_TLS);
    tls_descriptor.set_base(to_thread->thread_specific_data());
    tls_descriptor.set_limit(to_thread->thread_specific_region_size());
#else
    MSR fs_base_msr(MSR_FS_BASE);
    fs_base_msr.set(to_thread->thread_specific_data().get());
#endif

    if (from_regs.cr3 != to_regs.cr3)
        write_cr3(to_regs.cr3);

    to_thread->set_cpu(processor.get_id());

    auto in_critical = to_thread->saved_critical();
    VERIFY(in_critical > 0);
    processor.restore_in_critical(in_critical);

    if (has_fxsr)
        asm volatile("fxrstor %0" ::"m"(to_thread->fpu_state()));
    else
        asm volatile("frstor %0" ::"m"(to_thread->fpu_state()));

}

extern "C" FlatPtr do_init_context(Thread* thread, u32 flags)
{
    VERIFY_INTERRUPTS_DISABLED();
#if ARCH(I386)
    thread->regs().eflags = flags;
#else
    thread->regs().rflags = flags;
#endif
    return Processor::current().init_context(*thread, true);
}

void Processor::assume_context(Thread& thread, FlatPtr flags)
{
    dbgln_if(CONTEXT_SWITCH_DEBUG, "Assume context for thread {} {}", VirtualAddress(&thread), thread);

    VERIFY_INTERRUPTS_DISABLED();
    Scheduler::prepare_after_exec();

    VERIFY(Processor::in_critical() == 2);

    do_assume_context(&thread, flags);

    VERIFY_NOT_REACHED();
}

}