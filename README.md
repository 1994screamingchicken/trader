# Kraken Lua Trader

An automated cryptocurrency trading platform in C++ that executes user-written Lua trading scripts against the Kraken exchange API.

## Features

- **Kraken REST API** - Full authentication, order placement, market data retrieval, account balance queries
- **Kraken WebSocket** - Real-time market data streaming with automatic reconnection
- **Lua Scripting Engine** - Embedded Lua 5.4 (via sol2) with a safe, sandboxed trading API
- **Script Management** - Load, start, stop, pause, resume, and monitor multiple scripts concurrently
- **Safety Systems** - Configurable rate limiting, position limits, loss limits, and emergency stop
- **Paper Trading** - Test strategies without risking real funds
- **Configuration** - TOML-based configuration for API keys, scripts, and risk parameters

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                      Main Loop                           │
├──────────────────────────────────────────────────────────┤
│                   Script Manager                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │ Script 1 │  │ Script 2 │  │ Script N │  ...          │
│  │ (Lua VM) │  │ (Lua VM) │  │ (Lua VM) │              │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘              │
├───────┼──────────────┼──────────────┼────────────────────┤
│       │         Safety Layer        │                    │
│       │   ┌─────────────────────┐   │                    │
│       ├──►│    Rate Limiter     │◄──┤                    │
│       │   └─────────────────────┘   │                    │
│       │   ┌─────────────────────┐   │                    │
│       └──►│   Risk Manager      │◄──┘                    │
│           └─────────────────────┘                        │
├──────────────────────────────────────────────────────────┤
│              Kraken API Layer                             │
│  ┌─────────────────┐    ┌──────────────────────┐        │
│  │   REST Client   │    │  WebSocket Client    │        │
│  │  (libcurl/SSL)  │    │  (websocketpp/TLS)   │        │
│  └─────────────────┘    └──────────────────────┘        │
└──────────────────────────────────────────────────────────┘
```

## Project Structure

```
trader/
├── CMakeLists.txt              # Build system
├── README.md                   # This file
├── config/
│   └── trader.toml             # Configuration file
├── include/
│   ├── core/
│   │   ├── config.hpp          # Configuration structures
│   │   └── logger.hpp          # Logging system
│   ├── kraken/
│   │   ├── auth.hpp            # API authentication
│   │   ├── rest_client.hpp     # REST API client
│   │   └── websocket_client.hpp # WebSocket client
│   ├── safety/
│   │   ├── rate_limiter.hpp    # Rate limiting
│   │   └── risk_manager.hpp    # Risk management
│   └── scripting/
│       ├── lua_engine.hpp      # Lua VM wrapper
│       ├── sandbox.hpp         # Lua sandbox
│       └── script_manager.hpp  # Multi-script orchestration
├── scripts/                    # Example Lua trading scripts
│   ├── example_minimal.lua     # Basic template
│   ├── simple_momentum.lua     # Momentum strategy
│   ├── mean_reversion.lua      # Mean reversion strategy
│   └── grid_trader.lua         # Grid trading strategy
└── src/
    ├── main.cpp                # Application entry point
    ├── core/
    │   ├── config.cpp
    │   └── logger.cpp
    ├── kraken/
    │   ├── auth.cpp
    │   ├── rest_client.cpp
    │   └── websocket_client.cpp
    ├── safety/
    │   ├── rate_limiter.cpp
    │   └── risk_manager.cpp
    └── scripting/
        ├── lua_engine.cpp
        ├── sandbox.cpp
        └── script_manager.cpp
```

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [OpenSSL](https://www.openssl.org/) | 1.1+ | TLS, HMAC-SHA512 authentication |
| [libcurl](https://curl.se/libcurl/) | 7.68+ | HTTP/REST API requests |
| [Lua](https://www.lua.org/) | 5.4 | Scripting runtime |
| [sol2](https://github.com/ThePhD/sol2) | 3.3.0 | C++/Lua bindings (fetched by CMake) |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON parsing (fetched by CMake) |
| [spdlog](https://github.com/gabime/spdlog) | 1.12.0 | Logging (fetched by CMake) |
| [websocketpp](https://github.com/zaphoyd/websocketpp) | 0.8.2 | WebSocket client (fetched by CMake) |
| [toml++](https://github.com/marzer/tomlplusplus) | 3.4.0 | TOML config parsing (fetched by CMake) |
| [Boost.Asio](https://www.boost.org/) | 1.70+ | Async I/O for WebSocket |

## Build Instructions

### Prerequisites (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libssl-dev \
    libcurl4-openssl-dev \
    liblua5.4-dev \
    libboost-system-dev \
    libboost-thread-dev \
    pkg-config
```

### Prerequisites (macOS with Homebrew)

```bash
brew install cmake openssl curl lua boost pkg-config
```

### Prerequisites (Arch Linux)

```bash
sudo pacman -S cmake openssl curl lua boost pkgconf
```

### Build

```bash
cd trader
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

CMake will automatically fetch sol2, nlohmann/json, spdlog, websocketpp, and toml++ via `FetchContent`.

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Release | Build type (Debug, Release, RelWithDebInfo) |
| `USE_LUAJIT` | OFF | Use LuaJIT instead of Lua 5.4 |
| `BUILD_TESTS` | OFF | Build unit tests (requires Google Test) |

### LuaJIT Build (Alternative)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_LUAJIT=ON
make -j$(nproc)
```

## Configuration

Copy and edit the configuration file:

```bash
cp config/trader.toml config/my_config.toml
# Edit with your Kraken API keys and settings
```

### Configuration Sections

#### `[kraken]` - API Credentials

```toml
[kraken]
api_key = "YOUR_API_KEY"
api_secret = "YOUR_API_SECRET"
rest_url = "https://api.kraken.com"
ws_url = "wss://ws.kraken.com"
ws_auth_url = "wss://ws-auth.kraken.com"
```

#### `[risk]` - Risk Management

```toml
[risk]
max_position_size = 1.0       # Max position per asset
max_total_loss = 1000.0       # Emergency halt threshold (USD)
max_script_loss = 200.0       # Per-script loss limit (USD)
max_open_orders = 20          # Max concurrent orders
enforce_position_limits = true
enforce_loss_limits = true
```

#### `[rate_limit]` - API Rate Limiting

```toml
[rate_limit]
max_calls_per_second = 1
max_calls_per_minute = 15
cooldown_seconds = 30
per_script_calls_per_minute = 5
```

#### `[[scripts]]` - Script Definitions

```toml
[[scripts]]
name = "my_strategy"
path = "scripts/my_strategy.lua"
auto_start = true
[scripts.parameters]
pair = "XBT/USD"
quantity = "0.001"
```

## Usage

### Running

```bash
# Run with default config
./kraken_trader

# Run with custom config
./kraken_trader -c /path/to/config.toml

# Verbose mode (debug logging)
./kraken_trader -v

# Help
./kraken_trader -h
```

### Paper Trading

Set `paper_trading = true` in `[general]` to simulate orders without connecting to the exchange:

```toml
[general]
paper_trading = true
```

### Graceful Shutdown

- Press `Ctrl+C` once for graceful shutdown (stops all scripts, cancels orders)
- Press `Ctrl+C` twice to force exit

## Writing Trading Scripts

Scripts are Lua files that implement callback functions called by the engine.

### Script Lifecycle

```
load_script() → on_init() → [on_tick() loop] → on_stop()
                                  ↕
                            on_data(channel, pair, data)
```

### Required/Optional Callbacks

```lua
function on_init()
    -- Called once when script starts
    -- Initialize state, subscribe to data
end

function on_tick()
    -- Called every tick (configurable interval)
    -- Main trading logic goes here
end

function on_data(channel, pair, data)
    -- Called when WebSocket data arrives
    -- For real-time price updates
end

function on_stop()
    -- Called when script is stopped
    -- Cleanup: close positions, cancel orders
end
```

### Trading API (`trader` namespace)

| Function | Description |
|----------|-------------|
| `trader.market_order(pair, side, volume)` | Place a market order |
| `trader.limit_order(pair, side, volume, price)` | Place a limit order |
| `trader.cancel_order(txid)` | Cancel an order by ID |
| `trader.cancel_all()` | Cancel all open orders |
| `trader.get_ticker(pair)` | Get ticker data |
| `trader.get_ohlc(pair, interval)` | Get OHLC candle data |
| `trader.get_order_book(pair, depth)` | Get order book |
| `trader.get_balance()` | Get account balances |
| `trader.get_open_orders()` | Get open orders |
| `trader.get_positions()` | Get open positions |
| `trader.subscribe(channel, pairs)` | Subscribe to WebSocket data |
| `trader.unsubscribe(channel, pairs)` | Unsubscribe from data |
| `trader.timestamp()` | Get current time (ms) |
| `trader.sleep(ms)` | Sleep (max 5000ms) |
| `trader.name` | Script name (read-only) |

### Logging API (`log` namespace)

```lua
log.info("Message")
log.warn("Warning message")
log.error("Error message")
log.debug("Debug message")
```

### Parameters

Script parameters defined in `trader.toml` are accessible via the `params` table:

```lua
-- In config:
-- [scripts.parameters]
-- pair = "XBT/USD"
-- threshold = "0.5"

function on_init()
    local pair = params.pair          -- "XBT/USD"
    local threshold = params.threshold -- 0.5 (auto-converted to number)
end
```

### API Return Format

All `trader.*` API calls return a table with:

```lua
local result = trader.get_ticker("XBTUSD")
if result.success then
    local data = result.data  -- JSON response as Lua table
else
    log.error("Failed: " .. result.error)
end
```

### Sandbox Restrictions

For security, Lua scripts **cannot**:
- Access the filesystem (`io`, `os`, `dofile`, `loadfile`)
- Load external modules (`require`, `package`)
- Use the debug library
- Execute more than 10M instructions per callback (infinite loop protection)
- Sleep longer than 5 seconds per call

Scripts **can** use:
- Standard `string`, `table`, `math` libraries
- `print` (routed to logging)
- `pcall`, `xpcall`, `error`, `assert`
- `type`, `tostring`, `tonumber`, `pairs`, `ipairs`
- All functions in the `trader` and `log` namespaces

## Example Strategies

### Minimal Template (`scripts/example_minimal.lua`)
A bare-bones script demonstrating the API structure without placing orders.

### Simple Momentum (`scripts/simple_momentum.lua`)
Buys when price moves up by a threshold percentage, sells on equal downward moves.

### Mean Reversion (`scripts/mean_reversion.lua`)
Uses SMA and standard deviation to detect oversold/overbought conditions.

### Grid Trader (`scripts/grid_trader.lua`)
Places a grid of limit orders above and below current price, replacing filled orders.

## Safety & Risk Management

### Rate Limiting
- Global: configurable calls/second and calls/minute
- Per-script: configurable calls/minute per script
- Automatic cooldown period after hitting limits

### Position Limits
- Maximum position size per asset
- Configurable per-asset or globally

### Loss Limits
- Maximum total loss across all scripts (triggers emergency stop)
- Maximum loss per individual script (stops that script)
- Equity floor (halt if account value drops below threshold)

### Emergency Stop
Triggered automatically when:
- Total P&L exceeds `max_total_loss`
- Any script exceeds `max_script_loss`
- Account equity drops below `equity_floor`

Emergency stop:
1. Halts all running scripts
2. Cancels all open orders
3. Logs the event and exits with code 2

## Security Considerations

- **API keys**: Never commit `trader.toml` with real API keys. Use environment variables or a secrets manager in production.
- **Sandbox**: Lua scripts are sandboxed but review scripts before running them with real funds.
- **Paper trading**: Always test strategies in paper mode first.
- **Network**: The application only connects to Kraken's official API endpoints.

## License

MIT License - See LICENSE file for details.

## Disclaimer

This software is provided for educational and research purposes. Trading cryptocurrency carries significant financial risk. The authors are not responsible for any financial losses incurred through the use of this software. Always test thoroughly with paper trading before using real funds.
