#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::initrd::modules
{
    struct Module
    {
        const char* name;
        uint32_t start;
        uint32_t end;
        uint32_t size;
        uint32_t checksum;
        bool metadata_valid;
        bool name_valid;
    };

    void initialize(uint32_t multiboot_info_addr);
    bool is_ready();
    bool validation_passed();
    size_t declared_count();
    size_t count();
    size_t rejected_count();
    size_t truncated_count();
    const Module* at(size_t index);
    uint64_t total_bytes();
}
