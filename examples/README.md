# Example Trading Strategies

This folder contains example Lua scripts demonstrating different trading strategies for the Kraken trading platform.

Each script implements a complete strategy with entry/exit logic, position management, and risk controls. They serve as templates that can be customized with your own parameters.

## Strategies

| Script | Strategy | Description |
|--------|----------|-------------|
| `ema_crossover.lua` | EMA Crossover | Trend-following using fast/slow Exponential Moving Average crossovers (golden/death cross) |
| `rsi_strategy.lua` | RSI | Mean-reversion using Relative Strength Index overbought/oversold signals |
| `vwap_trader.lua` | VWAP | Institutional-style trading around Volume Weighted Average Price |
| `breakout.lua` | Breakout | Range detection with entry on price breaking above resistance or below support |
| `trailing_stop.lua` | Trailing Stop | Trend-following with dynamic trailing stop-loss that locks in profits |
| `scalper.lua` | Scalper | High-frequency small-profit trades using order book imbalance for direction |
| `order_book_imbalance.lua` | Order Book Imbalance | Microstructure strategy trading bid/ask volume imbalances |
| `dca_accumulator.lua` | DCA | Systematic accumulation at regular intervals with enhanced buys on dips |

## Usage

Copy any example to your scripts directory and configure it in your YAML config:

```yaml
scripts:
  - name: "my_ema_strategy"
    path: "examples/ema_crossover.lua"
    params:
      pair: "XBTUSD"
      fast_period: 9
      slow_period: 21
      quantity: 0.001
```

## Parameters

Each script documents its required parameters in the header comments. Common parameters:

- `pair` - Kraken trading pair (e.g., "XBTUSD", "ETHUSD")
- `quantity` - Order size in base currency units
- Strategy-specific thresholds and configuration values

## Risk Warning

These examples are for educational purposes only. They demonstrate strategy patterns and API usage. Before running any strategy with real funds:

1. Test thoroughly with small quantities
2. Understand the risk parameters (stop losses, position limits)
3. Monitor execution closely
4. Never risk more than you can afford to lose

## Script Structure

All scripts follow the same callback pattern:

```lua
function on_init()    -- Setup, subscribe to data, initialize state
function on_tick()    -- Main logic loop (called every tick interval)
function on_data()    -- Handle real-time WebSocket data
function on_stop()    -- Cleanup, flatten positions, unsubscribe
```

See the `scripts/example_minimal.lua` for the simplest possible template.
