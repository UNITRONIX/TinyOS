#pragma once

#include <stdint.h>

namespace tinyos::kernel::interrupts
{
	inline constexpr uint8_t IrqLineCount = 16;
	inline constexpr uint8_t NoIrq = 0xFF;

	void initialize_diagnostics();
	void record_irq(uint32_t irq);
	uint64_t irq_count(uint8_t irq);
	uint64_t total_irq_count();
	uint64_t unexpected_irq_count();
	uint8_t last_irq();
	bool has_seen_irq();
	bool hardware_irq_enabled();
	void set_hardware_irq_enabled(bool enabled);
}

// The full interrupted frame is passed so fault reports can include EIP, CS and
// EFLAGS. Without them a page fault or triple-fault candidate is untriageable.
extern "C" void interrupt_dispatch(uint32_t vector, uint32_t error_code, uint32_t eip, uint32_t cs, uint32_t eflags);
extern "C" void irq_dispatch(uint32_t irq);
