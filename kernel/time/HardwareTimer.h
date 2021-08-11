/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <base/Function.h>
#include <base/RefCounted.h>
#include <base/String.h>
#include <kernel/interrupts/IRQHandler.h>
#include <kernel/time/TimeManagement.h>

namespace Kernel {

enum class HardwareTimerType {
    i8253 = 0x1,                  
    RTC = 0x2,                    
    HighPrecisionEventTimer = 0x3,
    LocalAPICTimer = 0x4          
};

template<typename InterruptHandlerType>
class HardwareTimer;

class HardwareTimerBase
    : public RefCounted<HardwareTimerBase> {
public:
    virtual ~HardwareTimerBase() = default;

    virtual void will_be_destroyed() = 0;

    virtual StringView model() const = 0;
    virtual HardwareTimerType timer_type() const = 0;
    virtual Function<void(const RegisterState&)> set_callback(Function<void(const RegisterState&)>) = 0;

    virtual bool is_periodic() const = 0;
    virtual bool is_periodic_capable() const = 0;
    virtual void set_periodic() = 0;
    virtual void set_non_periodic() = 0;
    virtual void disable() = 0;
    virtual u32 frequency() const = 0;
    virtual bool can_query_raw() const { return false; }
    virtual u64 current_raw() const { return 0; }
    virtual u64 raw_to_ns(u64) const { return 0; }

    virtual size_t ticks_per_second() const = 0;

    virtual void reset_to_default_ticks_per_second() = 0;
    virtual bool try_to_set_frequency(size_t frequency) = 0;
    virtual bool is_capable_of_frequency(size_t frequency) const = 0;
    virtual size_t calculate_nearest_possible_frequency(size_t frequency) const = 0;
};

template<>
class HardwareTimer<IRQHandler>
    : public HardwareTimerBase
    , public IRQHandler {
public:
    virtual void will_be_destroyed() override
    {
        IRQHandler::will_be_destroyed();
    }

    virtual StringView purpose() const override
    {
        if (TimeManagement::the().is_system_timer(*this))
            return "System Timer";
        return model();
    }

    virtual Function<void(const RegisterState&)> set_callback(Function<void(const RegisterState&)> callback) override
    {
        disable_irq();
        auto previous_callback = move(m_callback);
        m_callback = move(callback);
        enable_irq();
        return previous_callback;
    }

    virtual u32 frequency() const override { return (u32)m_frequency; }

protected:
    HardwareTimer(u8 irq_number, Function<void(const RegisterState&)> callback = nullptr)
        : IRQHandler(irq_number)
        , m_callback(move(callback))
    {
    }

    virtual bool handle_irq(const RegisterState& regs) override
    {

        if (m_callback) {
            m_callback(regs);
            return true;
        }
        return false;
    }

    u64 m_frequency { OPTIMAL_TICKS_PER_SECOND_RATE };

private:
    Function<void(const RegisterState&)> m_callback;
};

template<>
class HardwareTimer<GenericInterruptHandler>
    : public HardwareTimerBase
    , public GenericInterruptHandler {
public:
    virtual void will_be_destroyed() override
    {
        GenericInterruptHandler::will_be_destroyed();
    }

    virtual StringView purpose() const override
    {
        return model();
    }

    virtual Function<void(const RegisterState&)> set_callback(Function<void(const RegisterState&)> callback) override
    {
        auto previous_callback = move(m_callback);
        m_callback = move(callback);
        return previous_callback;
    }

    virtual size_t sharing_devices_count() const override { return 0; }
    virtual bool is_shared_handler() const override { return false; }
    virtual bool is_sharing_with_others() const override { return false; }
    virtual HandlerType type() const override { return HandlerType::IRQHandler; }
    virtual StringView controller() const override { return nullptr; }
    virtual bool eoi() override;

    virtual u32 frequency() const override { return (u32)m_frequency; }

protected:
    HardwareTimer(u8 irq_number, Function<void(const RegisterState&)> callback = nullptr)
        : GenericInterruptHandler(irq_number)
        , m_callback(move(callback))
    {
    }

    virtual bool handle_interrupt(const RegisterState& regs) override
    {
        if (m_callback) {
            m_callback(regs);
            return true;
        }
        return false;
    }

    u64 m_frequency { OPTIMAL_TICKS_PER_SECOND_RATE };

private:
    Function<void(const RegisterState&)> m_callback;
};

}