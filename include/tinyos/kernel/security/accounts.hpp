#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::security::accounts
{
    struct Account
    {
        const char* name;
        uint32_t password_hash;
        bool admin;
    };

    void initialize();
    bool is_ready();
    size_t count();
    const Account* at(size_t index);
    bool authenticate(const char* name, const char* password);
    bool add_user(const char* name, const char* password, bool admin);
    uint32_t hash_password(const char* password);
    bool validation_self_test();
}
