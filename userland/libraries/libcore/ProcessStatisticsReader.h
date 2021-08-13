/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/String.h>
#include <libcore/File.h>
#include <unistd.h>

namespace Core {

struct ThreadStatistics {
    pid_t tid;
    unsigned times_scheduled;
    u64 time_user;
    u64 time_kernel;
    unsigned syscall_count;
    unsigned inode_faults;
    unsigned zero_faults;
    unsigned cow_faults;
    unsigned unix_socket_read_bytes;
    unsigned unix_socket_write_bytes;
    unsigned ipv4_socket_read_bytes;
    unsigned ipv4_socket_write_bytes;
    unsigned file_read_bytes;
    unsigned file_write_bytes;
    String state;
    u32 cpu;
    u32 priority;
    String name;
};

struct ProcessStatistics {

    pid_t pid;
    pid_t pgid;
    pid_t pgp;
    pid_t sid;
    uid_t uid;
    gid_t gid;
    pid_t ppid;
    unsigned nfds;
    bool kernel;
    String name;
    String executable;
    String tty;
    String pledge;
    String veil;
    size_t amount_virtual;
    size_t amount_resident;
    size_t amount_shared;
    size_t amount_dirty_private;
    size_t amount_clean_inode;
    size_t amount_purgeable_volatile;
    size_t amount_purgeable_nonvolatile;

    Vector<Core::ThreadStatistics> threads;

    // synthetic
    String username;
};

struct AllProcessesStatistics {
    Vector<ProcessStatistics> processes;
    u64 total_time_scheduled;
    u64 total_time_scheduled_kernel;
};

class ProcessStatisticsReader {
public:
    static Optional<AllProcessesStatistics> get_all(RefPtr<Core::File>&);
    static Optional<AllProcessesStatistics> get_all();

private:
    static String username_from_uid(uid_t);
    static HashMap<uid_t, String> s_usernames;
};

}
