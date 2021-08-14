/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <libjs/Bytecode/StringTable.h>

namespace JS::Bytecode {

StringTableIndex StringTable::insert(StringView string)
{
    for (size_t i = 0; i < m_strings.size(); i++) {
        if (m_strings[i] == string)
            return i;
    }
    m_strings.append(string);
    return m_strings.size() - 1;
}

String const& StringTable::get(StringTableIndex index) const
{
    return m_strings[index.value()];
}

void StringTable::dump() const
{
    outln("String Table:");
    for (size_t i = 0; i < m_strings.size(); i++)
        outln("{}: {}", i, m_strings[i]);
}

}