#pragma once

#include <ak/types.h>
#include "diskcontroller.h"

namespace Kernel {
    
    class diskController;

    enum diskType {
        hardDisk,
        usbDisk,
        floppy,
        cdROM
    };

    class Disk {
    public:
        diskController* controller;
        ak::uint32_t controllerIndex;
        char* identifier = 0;
        diskType type;
        ak::uint64_t size;
        ak::uint32_t numBlocks;
        ak::uint32_t blockSize;

        Disk(ak::uint32_t controllerIndex, diskController* controller, diskType type, ak::uint64_t size, ak::uint32_t blocks, ak::uint32_t blocksize);
            
        virtual char readSector(ak::uint32_t lba, ak::uint8_t* buf);
        virtual char writeSector(ak::uint32_t lba, ak::uint8_t* buf);
    };
    
}