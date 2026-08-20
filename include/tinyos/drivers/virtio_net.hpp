#pragma once

#include <stdint.h>

namespace tinyos::drivers::virtio_net
{
    void initialize();
    bool is_ready();
    bool link_up();
    uint64_t tx_packets();
    uint64_t rx_packets();
    bool validation_self_test();
}
