/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <EP/StdLibExtras.h>

namespace EP {

template<typename Callback>
class ScopeGuard {
public:
    ScopeGuard(Callback callback)
        : m_callback(move(callback))
    {
    }

    ~ScopeGuard()
    {
        m_callback();
    }

private:
    Callback m_callback;
};

template<typename Callback>
class ArmedScopeGuard {
public:
    ArmedScopeGuard(Callback callback)
        : m_callback(move(callback))
    {
    }

    ~ArmedScopeGuard()
    {
        if (m_armed)
            m_callback();
    }

    void disarm() { m_armed = false; }

private:
    Callback m_callback;
    bool m_armed { true };
};

}

using EP::ArmedScopeGuard;
using EP::ScopeGuard;
