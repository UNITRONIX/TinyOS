#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::admin::tools
{
    // Compatibility view over the live command registry.
    //
    // This used to be an independent table of 122 entries with its own
    // Ready/Planned flag, which drifted from the shell: it labelled roughly
    // forty unimplemented commands as Ready, referenced commands that no longer
    // existed (filemgr, textedit, helpsearch, status) and disagreed with the
    // shell's own help text about what was available (defect D21).
    //
    // It is now a read-only projection of libshell::registry. Every field comes
    // from the command's own descriptor, so the two can no longer disagree.
    // `find()` resolves through the registry, and `state` maps Availability onto
    // the historical Ready/Planned pair for callers that still use it.

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

    // Returns the projection at `index`. The returned pointer refers to a
    // function-local scratch buffer that is rewritten on every call, so callers
    // must consume it before the next registry query.
    const Tool* at(size_t index);

    // Pointer into the registry's own descriptor; not a scratch copy.
    const Tool* find(const char* command);

    bool validation_self_test();

    const char* category_name(Category category);
    const char* state_name(State state);
}
