#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::admin::tools
{
    enum class Category : uint32_t
    {
        Shell,
        Files,
        Storage,
        Devices,
        Memory,
        Runtime,
        Security,
        Ui,
        Scheduling,
        Power,
        Development
    };

    enum class State : uint32_t
    {
        Ready,
        Planned
    };

    struct Tool
    {
        const char* command;
        const char* purpose;
        Category category;
        State state;
        bool writes_state;
        bool high_risk;
    };

    void initialize();
    bool is_ready();
    size_t count();
    size_t ready_count();
    size_t planned_count();
    size_t write_tool_count();
    size_t high_risk_count();
    const Tool* at(size_t index);
    const Tool* find(const char* command);
    bool validation_self_test();
    const char* category_name(Category category);
    const char* state_name(State state);
}