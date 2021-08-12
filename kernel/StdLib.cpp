/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <base/Assertions.h>
#include <base/MemMem.h>
#include <base/String.h>
#include <base/Types.h>
#include <kernel/arch/x86/SmapDisabler.h>
#include <kernel/heap/kmalloc.h>
#include <kernel/memory/MemoryManager.h>
#include <kernel/StdLib.h>

String copy_string_from_user(const char* user_str, size_t user_str_size)
{
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(user_str), user_str_size);
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    void* fault_at;
    ssize_t length = Kernel::safe_strnlen(user_str, user_str_size, fault_at);
    if (length < 0) {
        dbgln("copy_string_from_user({:p}, {}) failed at {} (strnlen)", static_cast<const void*>(user_str), user_str_size, VirtualAddress { fault_at });
        return {};
    }
    if (length == 0)
        return String::empty();

    char* buffer;
    auto copied_string = StringImpl::create_uninitialized((size_t)length, buffer);
    if (!Kernel::safe_memcpy(buffer, user_str, (size_t)length, fault_at)) {
        dbgln("copy_string_from_user({:p}, {}) failed at {} (memcpy)", static_cast<const void*>(user_str), user_str_size, VirtualAddress { fault_at });
        return {};
    }
    return copied_string;
}

String copy_string_from_user(Userspace<const char*> user_str, size_t user_str_size)
{
    return copy_string_from_user(user_str.unsafe_userspace_ptr(), user_str_size);
}

Kernel::KResultOr<NonnullOwnPtr<Kernel::KString>> try_copy_kstring_from_user(const char* user_str, size_t user_str_size)
{
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(user_str), user_str_size);
    if (!is_user)
        return EFAULT;
    Kernel::SmapDisabler disabler;
    void* fault_at;
    ssize_t length = Kernel::safe_strnlen(user_str, user_str_size, fault_at);
    if (length < 0) {
        dbgln("copy_kstring_from_user({:p}, {}) failed at {} (strnlen)", static_cast<const void*>(user_str), user_str_size, VirtualAddress { fault_at });
        return EFAULT;
    }
    char* buffer;
    auto new_string = Kernel::KString::try_create_uninitialized(length, buffer);
    if (!new_string)
        return ENOMEM;

    buffer[length] = '\0';

    if (length == 0)
        return new_string.release_nonnull();

    if (!Kernel::safe_memcpy(buffer, user_str, (size_t)length, fault_at)) {
        dbgln("copy_kstring_from_user({:p}, {}) failed at {} (memcpy)", static_cast<const void*>(user_str), user_str_size, VirtualAddress { fault_at });
        return EFAULT;
    }
    return new_string.release_nonnull();
}

Kernel::KResultOr<NonnullOwnPtr<Kernel::KString>> try_copy_kstring_from_user(Userspace<const char*> user_str, size_t user_str_size)
{
    return try_copy_kstring_from_user(user_str.unsafe_userspace_ptr(), user_str_size);
}

[[nodiscard]] Optional<Time> copy_time_from_user(const timespec* ts_user)
{
    timespec ts;
    if (!copy_from_user(&ts, ts_user, sizeof(timespec))) {
        return {};
    }
    return Time::from_timespec(ts);
}
[[nodiscard]] Optional<Time> copy_time_from_user(const timeval* tv_user)
{
    timeval tv;
    if (!copy_from_user(&tv, tv_user, sizeof(timeval))) {
        return {};
    }
    return Time::from_timeval(tv);
}

template<>
[[nodiscard]] Optional<Time> copy_time_from_user<const timeval>(Userspace<const timeval*> src) { return copy_time_from_user(src.unsafe_userspace_ptr()); }
template<>
[[nodiscard]] Optional<Time> copy_time_from_user<timeval>(Userspace<timeval*> src) { return copy_time_from_user(src.unsafe_userspace_ptr()); }
template<>
[[nodiscard]] Optional<Time> copy_time_from_user<const timespec>(Userspace<const timespec*> src) { return copy_time_from_user(src.unsafe_userspace_ptr()); }
template<>
[[nodiscard]] Optional<Time> copy_time_from_user<timespec>(Userspace<timespec*> src) { return copy_time_from_user(src.unsafe_userspace_ptr()); }

Optional<u32> user_atomic_fetch_add_relaxed(volatile u32* var, u32 val)
{
    if (FlatPtr(var) & 3)
        return {}; // not aligned!
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_fetch_add_relaxed(var, val);
}

Optional<u32> user_atomic_exchange_relaxed(volatile u32* var, u32 val)
{
    if (FlatPtr(var) & 3)
        return {}; 
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_exchange_relaxed(var, val);
}

Optional<u32> user_atomic_load_relaxed(volatile u32* var)
{
    if (FlatPtr(var) & 3)
        return {}; 
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_load_relaxed(var);
}

bool user_atomic_store_relaxed(volatile u32* var, u32 val)
{
    if (FlatPtr(var) & 3)
        return false; 
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return false;
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_store_relaxed(var, val);
}

Optional<bool> user_atomic_compare_exchange_relaxed(volatile u32* var, u32& expected, u32 val)
{
    if (FlatPtr(var) & 3)
        return {}; 
    VERIFY(!Kernel::Memory::is_user_range(VirtualAddress(&expected), sizeof(expected)));
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_compare_exchange_relaxed(var, expected, val);
}

Optional<u32> user_atomic_fetch_and_relaxed(volatile u32* var, u32 val)
{
    if (FlatPtr(var) & 3)
        return {}; 
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_fetch_and_relaxed(var, val);
}

Optional<u32> user_atomic_fetch_and_not_relaxed(volatile u32* var, u32 val)
{
    if (FlatPtr(var) & 3)
        return {}; 
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_fetch_and_not_relaxed(var, val);
}

Optional<u32> user_atomic_fetch_or_relaxed(volatile u32* var, u32 val)
{
    if (FlatPtr(var) & 3)
        return {}; 
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_fetch_or_relaxed(var, val);
}

Optional<u32> user_atomic_fetch_xor_relaxed(volatile u32* var, u32 val)
{
    if (FlatPtr(var) & 3)
        return {}; 
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(FlatPtr(var)), sizeof(*var));
    if (!is_user)
        return {};
    Kernel::SmapDisabler disabler;
    return Kernel::safe_atomic_fetch_xor_relaxed(var, val);
}

extern "C" {

bool copy_to_user(void* dest_ptr, const void* src_ptr, size_t n)
{
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(dest_ptr), n);
    if (!is_user)
        return false;
    VERIFY(!Kernel::Memory::is_user_range(VirtualAddress(src_ptr), n));
    Kernel::SmapDisabler disabler;
    void* fault_at;
    if (!Kernel::safe_memcpy(dest_ptr, src_ptr, n, fault_at)) {
        VERIFY(VirtualAddress(fault_at) >= VirtualAddress(dest_ptr) && VirtualAddress(fault_at) <= VirtualAddress((FlatPtr)dest_ptr + n));
        dbgln("copy_to_user({:p}, {:p}, {}) failed at {}", dest_ptr, src_ptr, n, VirtualAddress { fault_at });
        return false;
    }
    return true;
}

bool copy_from_user(void* dest_ptr, const void* src_ptr, size_t n)
{
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(src_ptr), n);
    if (!is_user)
        return false;
    VERIFY(!Kernel::Memory::is_user_range(VirtualAddress(dest_ptr), n));
    Kernel::SmapDisabler disabler;
    void* fault_at;
    if (!Kernel::safe_memcpy(dest_ptr, src_ptr, n, fault_at)) {
        VERIFY(VirtualAddress(fault_at) >= VirtualAddress(src_ptr) && VirtualAddress(fault_at) <= VirtualAddress((FlatPtr)src_ptr + n));
        dbgln("copy_from_user({:p}, {:p}, {}) failed at {}", dest_ptr, src_ptr, n, VirtualAddress { fault_at });
        return false;
    }
    return true;
}

const void* memmem(const void* haystack, size_t haystack_length, const void* needle, size_t needle_length)
{
    return Base::memmem(haystack, haystack_length, needle, needle_length);
}

[[nodiscard]] bool memset_user(void* dest_ptr, int c, size_t n)
{
    bool is_user = Kernel::Memory::is_user_range(VirtualAddress(dest_ptr), n);
    if (!is_user)
        return false;
    Kernel::SmapDisabler disabler;
    void* fault_at;
    if (!Kernel::safe_memset(dest_ptr, c, n, fault_at)) {
        dbgln("memset_user({:p}, {}, {}) failed at {}", dest_ptr, c, n, VirtualAddress { fault_at });
        return false;
    }
    return true;
}

size_t strnlen(const char* str, size_t maxlen)
{
    size_t len = 0;
    for (; len < maxlen && *str; str++)
        len++;
    return len;
}

int strcmp(const char* s1, const char* s2)
{
    for (; *s1 == *s2; ++s1, ++s2) {
        if (*s1 == 0)
            return 0;
    }
    return *(const u8*)s1 < *(const u8*)s2 ? -1 : 1;
}

int memcmp(const void* v1, const void* v2, size_t n)
{
    auto* s1 = (const u8*)v1;
    auto* s2 = (const u8*)v2;
    while (n-- > 0) {
        if (*s1++ != *s2++)
            return s1[-1] < s2[-1] ? -1 : 1;
    }
    return 0;
}

int strncmp(const char* s1, const char* s2, size_t n)
{
    if (!n)
        return 0;
    do {
        if (*s1 != *s2++)
            return *(const unsigned char*)s1 - *(const unsigned char*)--s2;
        if (*s1++ == 0)
            break;
    } while (--n);
    return 0;
}

char* strstr(const char* haystack, const char* needle)
{
    char nch;
    char hch;

    if ((nch = *needle++) != 0) {
        size_t len = strlen(needle);
        do {
            do {
                if ((hch = *haystack++) == 0)
                    return nullptr;
            } while (hch != nch);
        } while (strncmp(haystack, needle, len) != 0);
        --haystack;
    }
    return const_cast<char*>(haystack);
}

[[noreturn]] void __stack_chk_fail() __attribute__((used));
[[noreturn]] void __stack_chk_fail_local() __attribute__((used));
extern "C" int __cxa_atexit(void (*)(void*), void*, void*);
[[noreturn]] void __cxa_pure_virtual();

[[noreturn]] void __stack_chk_fail()
{
    VERIFY_NOT_REACHED();
}

[[noreturn]] void __stack_chk_fail_local()
{
    VERIFY_NOT_REACHED();
}

extern "C" int __cxa_atexit(void (*)(void*), void*, void*)
{
    VERIFY_NOT_REACHED();
    return 0;
}

[[noreturn]] void __cxa_pure_virtual()
{
    VERIFY_NOT_REACHED();
}
}