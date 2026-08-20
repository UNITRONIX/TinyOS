#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/elf/loader.hpp>
#include <tinyos/kernel/initrd/modules.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    constexpr size_t MaxImages = 8;
    constexpr unsigned char ElfMagic0 = 0x7F;
    constexpr unsigned char ElfMagic1 = 'E';
    constexpr unsigned char ElfMagic2 = 'L';
    constexpr unsigned char ElfMagic3 = 'F';
    constexpr unsigned char ElfClass32 = 1;
    constexpr unsigned char ElfDataLittleEndian = 1;
    constexpr unsigned char ElfCurrentVersion = 1;
    constexpr uint16_t ElfTypeExecutable = 2;
    constexpr uint16_t ElfMachineI386 = 3;

    struct [[gnu::packed]] Elf32Header
    {
        unsigned char e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint32_t e_entry;
        uint32_t e_phoff;
        uint32_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    };

    struct [[gnu::packed]] Elf32ProgramHeader
    {
        uint32_t p_type;
        uint32_t p_offset;
        uint32_t p_vaddr;
        uint32_t p_paddr;
        uint32_t p_filesz;
        uint32_t p_memsz;
        uint32_t p_flags;
        uint32_t p_align;
    };

    tinyos::kernel::elf::loader::Image g_images[MaxImages] = {};
    size_t g_scanned_module_count = 0;
    size_t g_valid_image_count = 0;
    size_t g_raw_module_count = 0;
    size_t g_invalid_image_count = 0;
    bool g_validation_passed = false;
    bool g_ready = false;

    bool range_within_module(uint32_t offset, uint32_t size, uint32_t module_size)
    {
        if (size == 0 || offset > module_size)
        {
            return false;
        }

        return size <= module_size - offset;
    }

    bool has_elf_magic(const tinyos::kernel::initrd::modules::Module& module)
    {
        if (module.size < 4)
        {
            return false;
        }

        const auto* bytes = reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(module.start));
        return bytes[0] == ElfMagic0
            && bytes[1] == ElfMagic1
            && bytes[2] == ElfMagic2
            && bytes[3] == ElfMagic3;
    }

    bool is_valid_elf32(
        const tinyos::kernel::initrd::modules::Module& module,
        const Elf32Header*& header,
        tinyos::kernel::elf::loader::ImageStatus& status)
    {
        header = nullptr;
        status = tinyos::kernel::elf::loader::ImageStatus::RawModule;

        if (!module.metadata_valid)
        {
            status = tinyos::kernel::elf::loader::ImageStatus::InvalidModuleMetadata;
            return false;
        }

        if (!has_elf_magic(module))
        {
            return false;
        }

        if (module.size < sizeof(Elf32Header))
        {
            status = tinyos::kernel::elf::loader::ImageStatus::TruncatedHeader;
            return false;
        }

        header = reinterpret_cast<const Elf32Header*>(static_cast<uintptr_t>(module.start));
        if (header->e_ident[4] != ElfClass32
            || header->e_ident[5] != ElfDataLittleEndian
            || header->e_ident[6] != ElfCurrentVersion
            || header->e_version != ElfCurrentVersion
            || header->e_ehsize != sizeof(Elf32Header))
        {
            status = tinyos::kernel::elf::loader::ImageStatus::UnsupportedFormat;
            return false;
        }

        if (header->e_type != ElfTypeExecutable)
        {
            status = tinyos::kernel::elf::loader::ImageStatus::UnsupportedType;
            return false;
        }

        if (header->e_machine != ElfMachineI386)
        {
            status = tinyos::kernel::elf::loader::ImageStatus::UnsupportedMachine;
            return false;
        }

        if (header->e_entry == 0)
        {
            status = tinyos::kernel::elf::loader::ImageStatus::InvalidEntryPoint;
            return false;
        }

        const uint32_t program_header_bytes = static_cast<uint32_t>(header->e_phentsize) * header->e_phnum;
        if (header->e_phnum == 0
            || header->e_phentsize != sizeof(Elf32ProgramHeader)
            || !range_within_module(header->e_phoff, program_header_bytes, module.size))
        {
            status = tinyos::kernel::elf::loader::ImageStatus::InvalidProgramHeaders;
            return false;
        }

        status = tinyos::kernel::elf::loader::ImageStatus::ValidElf32;
        return true;
    }

    void fill_valid_elf_test_image(unsigned char* buffer, size_t size)
    {
        for (size_t index = 0; index < size; ++index)
        {
            buffer[index] = 0;
        }

        auto* header = reinterpret_cast<Elf32Header*>(buffer);
        header->e_ident[0] = ElfMagic0;
        header->e_ident[1] = ElfMagic1;
        header->e_ident[2] = ElfMagic2;
        header->e_ident[3] = ElfMagic3;
        header->e_ident[4] = ElfClass32;
        header->e_ident[5] = ElfDataLittleEndian;
        header->e_ident[6] = ElfCurrentVersion;
        header->e_type = ElfTypeExecutable;
        header->e_machine = ElfMachineI386;
        header->e_version = ElfCurrentVersion;
        header->e_entry = 0x1000;
        header->e_phoff = sizeof(Elf32Header);
        header->e_ehsize = sizeof(Elf32Header);
        header->e_phentsize = sizeof(Elf32ProgramHeader);
        header->e_phnum = 1;
    }
}

namespace tinyos::kernel::elf::loader
{
    void initialize()
    {
        g_scanned_module_count = 0;
        g_valid_image_count = 0;
        g_raw_module_count = 0;
        g_invalid_image_count = 0;
        g_validation_passed = false;
        g_ready = false;

        const size_t module_count = initrd::modules::count();
        const size_t limit = module_count < MaxImages ? module_count : MaxImages;

        for (size_t index = 0; index < limit; ++index)
        {
            const auto* module = initrd::modules::at(index);
            if (module == nullptr)
            {
                continue;
            }

            const Elf32Header* header = nullptr;
            ImageStatus status = ImageStatus::RawModule;
            g_images[g_scanned_module_count].name = module->name;
            g_images[g_scanned_module_count].size = module->size;
            g_images[g_scanned_module_count].valid = is_valid_elf32(*module, header, status);
            g_images[g_scanned_module_count].status = status;
            g_images[g_scanned_module_count].type = header != nullptr ? header->e_type : 0;
            g_images[g_scanned_module_count].machine = header != nullptr ? header->e_machine : 0;
            g_images[g_scanned_module_count].program_header_offset = header != nullptr ? header->e_phoff : 0;
            g_images[g_scanned_module_count].program_header_count = header != nullptr ? header->e_phnum : 0;
            g_images[g_scanned_module_count].entry_point = header != nullptr ? header->e_entry : 0;

            if (g_images[g_scanned_module_count].valid)
            {
                ++g_valid_image_count;
            }
            else if (status == ImageStatus::RawModule)
            {
                ++g_raw_module_count;
            }
            else
            {
                ++g_invalid_image_count;
            }

            ++g_scanned_module_count;
        }

        g_validation_passed = g_invalid_image_count == 0;
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "ELF loader scaffold initialized.");
        kernel::klog::write_line(kernel::klog::Level::Info, "ELF loader validation scaffold ready.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    size_t scanned_module_count()
    {
        return g_scanned_module_count;
    }

    size_t valid_image_count()
    {
        return g_valid_image_count;
    }

    size_t raw_module_count()
    {
        return g_raw_module_count;
    }

    size_t invalid_image_count()
    {
        return g_invalid_image_count;
    }

    bool validation_passed()
    {
        return g_validation_passed;
    }

    bool load_image(size_t index, uintptr_t& entry_out)
    {
        entry_out = 0;
        if (!g_ready || index >= g_scanned_module_count || !g_images[index].valid)
        {
            return false;
        }

        const auto* module = initrd::modules::at(index);
        if (module == nullptr)
        {
            return false;
        }

        const auto* header = reinterpret_cast<const Elf32Header*>(static_cast<uintptr_t>(module->start));
        const auto* program_headers = reinterpret_cast<const Elf32ProgramHeader*>(
            static_cast<uintptr_t>(module->start) + header->e_phoff);

        for (uint16_t ph = 0; ph < header->e_phnum; ++ph)
        {
            if (program_headers[ph].p_type != 1)
            {
                continue;
            }

            auto* destination = reinterpret_cast<unsigned char*>(program_headers[ph].p_vaddr);
            const auto* source = reinterpret_cast<const unsigned char*>(
                static_cast<uintptr_t>(module->start) + program_headers[ph].p_offset);
            for (uint32_t byte_index = 0; byte_index < program_headers[ph].p_filesz; ++byte_index)
            {
                destination[byte_index] = source[byte_index];
            }

            for (uint32_t byte_index = program_headers[ph].p_filesz; byte_index < program_headers[ph].p_memsz; ++byte_index)
            {
                destination[byte_index] = 0;
            }
        }

        entry_out = header->e_entry;
        return true;
    }

    bool validation_self_test()
    {
        unsigned char raw_buffer[8];
        unsigned char valid_elf_buffer[sizeof(Elf32Header) + sizeof(Elf32ProgramHeader)];
        unsigned char bad_machine_buffer[sizeof(Elf32Header) + sizeof(Elf32ProgramHeader)];
        for (size_t index = 0; index < sizeof(raw_buffer); ++index)
        {
            raw_buffer[index] = 0;
        }

        fill_valid_elf_test_image(valid_elf_buffer, sizeof(valid_elf_buffer));
        fill_valid_elf_test_image(bad_machine_buffer, sizeof(bad_machine_buffer));
        reinterpret_cast<Elf32Header*>(bad_machine_buffer)->e_machine = 0xFFFF;

        const initrd::modules::Module raw_module = {
            "raw-test",
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(raw_buffer)),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(raw_buffer) + sizeof(raw_buffer)),
            sizeof(raw_buffer),
            0,
            true,
            true
        };
        const initrd::modules::Module valid_module = {
            "elf-test",
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(valid_elf_buffer)),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(valid_elf_buffer) + sizeof(valid_elf_buffer)),
            sizeof(valid_elf_buffer),
            0,
            true,
            true
        };
        const initrd::modules::Module invalid_module = {
            "bad-machine-test",
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bad_machine_buffer)),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bad_machine_buffer) + sizeof(bad_machine_buffer)),
            sizeof(bad_machine_buffer),
            0,
            true,
            true
        };

        const Elf32Header* header = nullptr;
        ImageStatus status = ImageStatus::RawModule;
        const bool raw_ok = !is_valid_elf32(raw_module, header, status) && status == ImageStatus::RawModule;
        const bool valid_ok = is_valid_elf32(valid_module, header, status) && status == ImageStatus::ValidElf32;
        const bool invalid_ok = !is_valid_elf32(invalid_module, header, status) && status == ImageStatus::UnsupportedMachine;

        return raw_ok && valid_ok && invalid_ok;
    }

    const char* status_name(ImageStatus status)
    {
        switch (status)
        {
        case ImageStatus::RawModule:
            return "raw";
        case ImageStatus::ValidElf32:
            return "valid-elf32";
        case ImageStatus::InvalidModuleMetadata:
            return "invalid-module-metadata";
        case ImageStatus::TruncatedHeader:
            return "truncated-header";
        case ImageStatus::UnsupportedFormat:
            return "unsupported-format";
        case ImageStatus::UnsupportedType:
            return "unsupported-type";
        case ImageStatus::UnsupportedMachine:
            return "unsupported-machine";
        case ImageStatus::InvalidProgramHeaders:
            return "invalid-program-headers";
        case ImageStatus::InvalidEntryPoint:
            return "invalid-entry";
        }

        return "unknown";
    }

    const Image* image_at(size_t index)
    {
        if (index >= g_scanned_module_count)
        {
            return nullptr;
        }

        return &g_images[index];
    }
}
