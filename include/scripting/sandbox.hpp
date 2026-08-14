#pragma once

#include <sol/sol.hpp>
#include <vector>
#include <string>

namespace trader {

/// Apply sandbox restrictions to a Lua state.
/// Removes filesystem, network, OS, and other dangerous capabilities.
/// Only whitelisted safe standard library functions remain.
void apply_lua_sandbox(sol::state& lua);

/// Get list of removed/blocked functions (for logging)
std::vector<std::string> get_blocked_functions();

/// Set maximum memory usage for the Lua state (bytes, 0 = unlimited)
void set_lua_memory_limit(sol::state& lua, size_t max_bytes);

/// Set maximum instruction count before forced yield (0 = unlimited)
void set_lua_instruction_limit(sol::state& lua, int max_instructions);

}  // namespace trader
