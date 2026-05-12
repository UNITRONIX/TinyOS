#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::vfs
{
    struct Node
    {
        const char* name;
        bool directory;
        const char* readonly_data;
        char* writable_data;
        size_t size;
        size_t capacity;
        bool writable;
        const Node* parent;
    };

    void initialize();
    bool is_ready();
    bool block_mount_ready();
    bool validate_path(const char* path);
    bool validation_self_test();
    const Node* root();
    const Node* find(const char* path);
    size_t child_count(const Node* node);
    const Node* child_at(const Node* node, size_t index);
    bool read_file(const Node* node, const char*& data, size_t& size);
    bool write_file(const char* path, const char* data, size_t size);
    bool create_directory(const char* path);
    bool create_file(const char* path);
    bool remove(const char* path);
    bool copy_file(const char* source_path, const char* destination_path);
    bool move(const char* source_path, const char* destination_path);
    uint16_t access_mode(const Node* node);
    bool can_enter_directory(const Node* node);
    bool can_list_directory(const Node* node);
    bool can_modify_directory(const Node* node);
    bool set_access_mode(const char* path, uint16_t mode);
}
