/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/Function.h>
#include <libcore/Object.h>

namespace Core {

class Notifier : public Object {
    C_OBJECT(Notifier)
public:
    enum Event {
        None = 0,
        Read = 1,
        Write = 2,
        Exceptional = 4,
    };

    virtual ~Notifier() override;

    void set_enabled(bool);

    Function<void()> on_ready_to_read;
    Function<void()> on_ready_to_write;

    void close();

    int fd() const { return m_fd; }
    unsigned event_mask() const { return m_event_mask; }
    void set_event_mask(unsigned event_mask) { m_event_mask = event_mask; }

    void event(Core::Event&) override;

private:
    Notifier(int fd, unsigned event_mask, Object* parent = nullptr);

    int m_fd { -1 };
    unsigned m_event_mask { 0 };
};

}
