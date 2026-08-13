cmake_minimum_required(VERSION 3.20)
project(KrakenLuaTrader VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_TESTS "Build unit tests" OFF)
option(USE_LUAJIT "Use LuaJIT instead of standard Lua" OFF)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Fetch dependencies via FetchContent
include(FetchContent)

# nlohmann/json for JSON parsing
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# sol2 for Lua bindings (develop branch fixes GCC 13+ template-body issues)
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG develop
)

# spdlog for logging
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)

# TOML++ for configuration files
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)

# FTXUI for terminal user interface
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
)

FetchContent_MakeAvailable(json sol2 spdlog tomlplusplus ftxui)

# Find Boost (Beast is header-only, part of Boost since 1.66)
find_package(Boost 1.66 REQUIRED)

# Find Lua
if(USE_LUAJIT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUAJIT REQUIRED luajit)
    set(LUA_INCLUDE_DIRS ${LUAJIT_INCLUDE_DIRS})
    set(LUA_LIBRARIES ${LUAJIT_LIBRARIES})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUA lua5.4)
    if(NOT LUA_FOUND)
        pkg_check_modules(LUA lua54)
    endif()
    if(NOT LUA_FOUND)
        pkg_check_modules(LUA lua-5.4)
    endif()
    if(NOT LUA_FOUND)
        pkg_check_modules(LUA REQUIRED lua>=5.4)
    endif()
    set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIRS})
    set(LUA_LIBRARIES ${LUA_LIBRARIES})
endif()

# Define the main library
add_library(trader_lib STATIC
    # Core
    src/core/config.cpp
    src/core/logger.cpp
    # Kraken
    src/kraken/rest_client.cpp
    src/kraken/websocket_client.cpp
    src/kraken/auth.cpp
    # Scripting
    src/scripting/lua_engine.cpp
    src/scripting/script_manager.cpp
    src/scripting/sandbox.cpp
    # Safety
    src/safety/rate_limiter.cpp
    src/safety/risk_manager.cpp
    # TUI
    src/tui/tui_app.cpp
    src/tui/log_sink.cpp
)

target_include_directories(trader_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LUA_INCLUDE_DIRS}
)
target_include_directories(trader_lib SYSTEM PUBLIC
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(trader_lib PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    Threads::Threads
    nlohmann_json::nlohmann_json
    sol2
    spdlog::spdlog
    tomlplusplus::tomlplusplus
    ftxui::screen
    ftxui::dom
    ftxui::component
    ${LUA_LIBRARIES}
)

# Main executable
add_executable(kraken_trader src/main.cpp)
target_link_libraries(kraken_trader PRIVATE trader_lib)

# Install rules
install(TARGETS kraken_trader DESTINATION bin)
install(DIRECTORY scripts/ DESTINATION share/kraken_trader/scripts)
install(DIRECTORY examples/ DESTINATION share/kraken_trader/examples)
install(FILES config/trader.toml DESTINATION etc/kraken_trader)

# Tests (optional)
if(BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()
