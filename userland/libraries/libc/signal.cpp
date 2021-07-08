/*
 * Copyright (c) 2021, krishpranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <libabi/Syscalls.h>
#include <assert.h>
#include <signal.h>

sighandler_t signal(int sig, sighandler_t handler)
{

    UNUSED(sig);
    UNUSED(handler);

    ASSERT_NOT_REACHED();

    return NULL;
}