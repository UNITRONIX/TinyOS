#pragma once

#include <stdint.h>

namespace tinyos::drivers::usb_hid
{
    void initialize();
    bool is_ready();
    bool keyboard_present();
    bool poll_key(char& out);
    bool validation_self_test();
}
