#pragma once

namespace inasm64
{
    constexpr auto kXedInstrutionTableSize = 1579;
    constexpr auto kXedInstrutionPrefixTableSize = 23;
    extern const char* xed_instruction_table[1579];
    struct xed_instruction_prefix_t
    {
        char _prefix;
        short _index;
    };
    extern const xed_instruction_prefix_t xed_instruction_prefix_table[23];
}  // namespace inasm64
