#pragma once

#include <stddef.h>

#include <tinyos/kernel/vfs/vfs.hpp>

namespace tinyos::kernel::vfs::ramfs
{
    void initialize();
    bool is_ready();
    const Node* root();
    const Node* find(const char* path);
    size_t child_count(const Node* node);
    const Node* child_at(const Node* node, size_t index);
    bool read_file(const Node* node, const char*& data, size_t& size);
    bool write_file(const char* path, const char* data, size_t size);
}
