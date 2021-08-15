/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/String.h>
#include <libcore/DateTime.h>

namespace Web::Cookie {

enum class Source {
    NonHttp,
    Http,
};

struct Cookie {
    String name;
    String value;
    Core::DateTime creation_time {};
    Core::DateTime last_access_time {};
    Core::DateTime expiry_time {};
    String domain {};
    String path {};
    bool secure { false };
    bool http_only { false };
    bool host_only { false };
    bool persistent { false };
};

}
