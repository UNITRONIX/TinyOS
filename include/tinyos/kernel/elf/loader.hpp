#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::elf::loader
{
    enum class ImageStatus : uint32_t
    {
        RawModule,
        ValidElf32,
        InvalidModuleMetadata,
        TruncatedHeader,
        UnsupportedFormat,
        UnsupportedType,
        UnsupportedMachine,
        InvalidProgramHeaders,
        InvalidEntryPoint
    };

    struct Image
    {
        const char* name;
        bool valid;
        ImageStatus status;
        uint16_t type;
        uint16_t machine;
        uint32_t program_header_offset;
        uint16_t program_header_count;
        uint32_t entry_point;
        uint32_t size;
    };

    void initialize();
    bool is_ready();
    size_t scanned_module_count();
    size_t valid_image_count();
    size_t raw_module_count();
    size_t invalid_image_count();
    bool validation_passed();
    bool validation_self_test();
    const char* status_name(ImageStatus status);
    const Image* image_at(size_t index);
}
