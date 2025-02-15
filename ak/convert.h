#pragma once

#include "types.h"
#include "memoperator.h"

namespace ak {
    class Convert {
    public:
        static char* intToString(int i);
        static char* intToString32(uint32_t i);

        static char* intToHexString(ak::uint8_t w);
        static char* intToHexString(ak::uint16_t w);
        static char* intToHexString(ak::uint32_t w);

        static int stringToInt(char* string);
        static uint32_t hexToInt(char* string);
    };
}