#pragma once

#include <stddef.h>

namespace tinyos::kernel::vfs::mount
{
    void initialize();
    bool mount(const char* source, const char* target);
    bool unmount(const char* target);
    bool resolve(const char* path, char* resolved, size_t resolved_capacity);
    bool is_mounted(const char* target);
    size_t active_count();
    bool active_at(size_t index, const char*& source, const char*& target);
    bool validation_self_test();
    void auto_mount_persistent_layout();
    bool layout_validation_self_test();
}
