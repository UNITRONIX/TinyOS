#pragma once

#include <stdint.h>

extern "C" void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr);
