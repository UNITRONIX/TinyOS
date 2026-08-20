#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/vfs/vfs.hpp>

namespace tinyos::kernel::vfs::fatfs
{
    void initialize();
    bool is_ready();
    bool format_if_needed();
    bool mount();
    const char* mount_path();
    const Node* root();
    bool owns(const Node* node);
    const Node* find(const char* path);
    size_t child_count(const Node* node);
    const Node* child_at(const Node* node, size_t index);
    bool write_file(const char* name, const char* data, size_t size);
    bool read_file(const char* name, char* buffer, size_t capacity, size_t& out_size);
    bool validation_self_test();
}
