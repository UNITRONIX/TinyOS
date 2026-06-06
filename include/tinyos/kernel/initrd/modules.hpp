#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/vfs/vfs.hpp>

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
    void mount_vfs();
    bool is_ready();
    bool validation_passed();
    bool vfs_ready();
    size_t declared_count();
    size_t count();
    size_t rejected_count();
    size_t truncated_count();
    size_t vfs_file_count();
    const Module* at(size_t index);
    uint64_t total_bytes();
    const vfs::Node* vfs_root();
    const vfs::Node* vfs_find(const char* path);
    bool vfs_owns(const vfs::Node* node);
    size_t vfs_child_count(const vfs::Node* node);
    const vfs::Node* vfs_child_at(const vfs::Node* node, size_t index);
    bool vfs_read_file(const vfs::Node* node, const char*& data, size_t& size);
    bool vfs_validation_self_test();
    const char* vfs_mount_path();
}
