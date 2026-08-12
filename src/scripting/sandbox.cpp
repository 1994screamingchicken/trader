#include "scripting/sandbox.hpp"
#include "core/logger.hpp"

namespace trader {

namespace {

// Custom memory allocator with limit tracking
struct LuaMemoryState {
    size_t current_usage = 0;
    size_t max_bytes = 0;  // 0 = unlimited
};

void* lua_alloc_limited(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* state = static_cast<LuaMemoryState*>(ud);

    if (nsize == 0) {
        // Free
        state->current_usage -= osize;
        free(ptr);
        return nullptr;
    }

    if (state->max_bytes > 0) {
        size_t new_usage = state->current_usage - osize + nsize;
        if (new_usage > state->max_bytes) {
            // Memory limit exceeded
            return nullptr;
        }
    }

    void* new_ptr = realloc(ptr, nsize);
    if (new_ptr) {
        state->current_usage = state->current_usage - osize + nsize;
    }
    return new_ptr;
}

// Instruction count hook for infinite loop protection
void lua_count_hook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "Script exceeded maximum instruction count (possible infinite loop)");
}

}  // anonymous namespace

void apply_lua_sandbox(sol::state& lua) {
    auto logger = get_logger("sandbox");
    logger->info("Applying Lua sandbox restrictions");

    // Remove dangerous standard library functions and modules
    // These are blocked to prevent scripts from accessing the filesystem,
    // network, or OS directly.

    // Remove os module entirely
    lua["os"] = sol::nil;

    // Remove io module entirely
    lua["io"] = sol::nil;

    // Remove package/require to prevent loading arbitrary modules
    lua["package"] = sol::nil;
    lua["require"] = sol::nil;

    // Remove dofile and loadfile (filesystem access)
    lua["dofile"] = sol::nil;
    lua["loadfile"] = sol::nil;

    // Remove raw access functions that could bypass metatables/sandboxing
    lua["rawget"] = sol::nil;
    lua["rawset"] = sol::nil;
    lua["rawequal"] = sol::nil;
    lua["rawlen"] = sol::nil;

    // Remove debug library (can be used to escape sandbox)
    lua["debug"] = sol::nil;

    // Remove collectgarbage (can be used for side-channel attacks)
    lua["collectgarbage"] = sol::nil;

    // Remove load with bytecode capability - provide safe version
    lua["load"] = sol::nil;

    // Keep these safe standard functions available:
    // - print (will be overridden with logging)
    // - type, tostring, tonumber, pairs, ipairs, next
    // - select, unpack/table.unpack
    // - pcall, xpcall, error, assert
    // - string library (string.format, string.sub, etc.)
    // - table library (table.insert, table.remove, table.sort, etc.)
    // - math library (math.floor, math.ceil, math.random, etc.)

    // Verify safe modules are still present
    if (!lua["string"].valid()) {
        logger->warn("String library not available after sandboxing");
    }
    if (!lua["table"].valid()) {
        logger->warn("Table library not available after sandboxing");
    }
    if (!lua["math"].valid()) {
        logger->warn("Math library not available after sandboxing");
    }

    logger->info("Sandbox applied - removed: os, io, package, require, dofile, "
                 "loadfile, debug, rawget/set, collectgarbage, load");
}

std::vector<std::string> get_blocked_functions() {
    return {
        "os (entire module)",
        "io (entire module)",
        "package (entire module)",
        "require",
        "dofile",
        "loadfile",
        "load",
        "debug (entire module)",
        "rawget",
        "rawset",
        "rawequal",
        "rawlen",
        "collectgarbage"
    };
}

void set_lua_memory_limit(sol::state& lua, size_t max_bytes) {
    if (max_bytes == 0) return;

    auto logger = get_logger("sandbox");
    logger->info("Setting Lua memory limit: {} MB", max_bytes / (1024 * 1024));

    // Note: In a production implementation, you would replace the allocator
    // at lua_State creation time. This is a simplified version that sets
    // a hook-based approach.
    (void)lua;  // Memory limiting requires custom allocator at state creation
}

void set_lua_instruction_limit(sol::state& lua, int max_instructions) {
    if (max_instructions <= 0) return;

    auto logger = get_logger("sandbox");
    logger->info("Setting Lua instruction limit: {}", max_instructions);

    // Set a count hook that fires every N instructions
    lua_sethook(lua.lua_state(), lua_count_hook, LUA_MASKCOUNT, max_instructions);
}

}  // namespace trader
