# Backtesting Guide

The backtesting engine replays historical OHLC data through your Lua trading scripts without modifications. Your scripts use the same `trader.*` API they would use in live or paper trading, but all data comes from the CSV file and all orders are simulated.

## CSV Data Format

The backtesting engine expects a CSV file with the following columns:

```
timestamp,open,high,low,close,volume
```

- **timestamp**: Unix timestamp (seconds since epoch)
- **open**: Opening price for the candle
- **high**: Highest price during the candle
- **low**: Lowest price during the candle
- **close**: Closing price for the candle
- **volume**: Trading volume during the candle

The first line is treated as a header if it starts with a non-numeric character.

### Example CSV

```csv
timestamp,open,high,low,close,volume
1704067200,42500.00,42850.00,42350.00,42780.00,125.43
1704070800,42780.00,43100.00,42680.00,43050.00,98.76
```

## Getting Historical Data from Kraken

### Automated Download Tool

The `tools/download_history.py` script downloads months of historical OHLC data from the Kraken public API. It handles pagination automatically (Kraken returns ~720 candles per request) by walking forward using the `since` parameter, deduplicates overlapping candles, and saves a sorted CSV ready for backtesting.

```bash
# Download 3 months of hourly BTC data
python3 tools/download_history.py --pair XBTUSD --interval 60 --months 3

# Download 6 months of 15-min ETH data
python3 tools/download_history.py --pair ETHUSD --interval 15 --months 6

# Download 1 month of daily SOL data
python3 tools/download_history.py --pair SOLUSD --interval 1440 --months 1
```

#### Options

| Flag | Default | Description |
|------|---------|-------------|
| `--pair <pair>` | `XBTUSD` | Kraken trading pair |
| `--interval <min>` | `60` | Candle interval in minutes (1, 5, 15, 30, 60, 240, 1440, 10080, 21600) |
| `--months <n>` | `3` | Number of months of history to download |
| `--output <path>` | `data/{pair}_{interval}m.csv` | Output CSV file path |

The tool prints progress as it downloads:

```
Downloading XBTUSD OHLC data (interval=60m)
Period: 3 months (2024-10-14 to now)
Estimated candles: ~2160
Output: data/XBTUSD_60m.csv

Downloaded 720/2160 candles (33%)
Downloaded 1440/2160 candles (66%)
Downloaded 2160/2160 candles (100%)

Total candles collected: 2160
API requests made: 3
Date range: 2024-10-14 00:00 to 2025-01-13 23:00 UTC
Saved to: data/XBTUSD_60m.csv
```

After downloading, run a backtest:

```bash
./kraken_trader --backtest --data ../data/XBTUSD_60m.csv --script ../scripts/top_crypto_trader.lua --pair XBT/USD --balance 10000
```

### Manual Download (Single Request)

For quick tests, you can download a single batch of ~720 candles directly:

```bash
# Get hourly (interval=60) BTC/USD candles
curl -s "https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval=60" | \
  python3 -c "
import json, sys
data = json.load(sys.stdin)['result']
pair = [k for k in data if k != 'last'][0]
print('timestamp,open,high,low,close,volume')
for row in data[pair]:
    print(f'{row[0]},{row[1]},{row[2]},{row[3]},{row[4]},{row[6]}')
" > data/btc_1h.csv
```

Available intervals (in minutes): 1, 5, 15, 30, 60, 240, 1440, 10080, 21600

## Command-Line Usage

```bash
./kraken_trader --backtest [OPTIONS]
```

### Required Options

| Flag | Description |
|------|-------------|
| `--data <path>` | Path to the OHLC CSV data file |
| `--script <path>` | Path to the Lua trading strategy script |

### Optional Options

| Flag | Default | Description |
|------|---------|-------------|
| `--pair <pair>` | `XBT/USD` | Trading pair name (must match what your script uses) |
| `--balance <amount>` | `10000` | Initial cash balance in quote currency (e.g., USD) |
| `--commission <pct>` | `0.1` | Commission as a percentage of trade value |

## Example Invocation

```bash
# Run backtest with sample data
./kraken_trader --backtest \
  --data data/sample_btc_1h.csv \
  --script scripts/simple_momentum.lua \
  --pair XBT/USD \
  --balance 10000 \
  --commission 0.1
```

## Understanding the Performance Report

After a backtest completes, a performance report is printed:

```
======================================================
              BACKTEST PERFORMANCE REPORT
======================================================

  Period:           2024-01-01 00:00:00 to 2024-01-05 00:00:00
  Candles:          100

  --- Profit & Loss ---
  Initial Balance:  $10000.00
  Final Equity:     $10250.00
  Total P&L:        $250.00 (+2.50%)

  --- Trade Statistics ---
  Total Trades:     12
  Winning Trades:   7
  Losing Trades:    5
  Win Rate:         58.33%
  Profit Factor:    1.85

  --- Risk Metrics ---
  Max Drawdown:     $150.00 (1.48%)
  Sharpe Ratio:     1.234
```

### Metric Definitions

- **Total P&L**: Final equity minus initial balance
- **Return %**: Percentage return on initial balance
- **Win Rate**: Percentage of closing trades that were profitable
- **Profit Factor**: Gross profits divided by gross losses (>1 is profitable)
- **Max Drawdown**: Largest peak-to-trough decline in equity
- **Max Drawdown %**: Max drawdown as percentage of peak equity
- **Sharpe Ratio**: Risk-adjusted return (annualized, assumes hourly candles)

## How It Works

1. The engine loads your CSV data into memory
2. A simulated broker is created with your specified initial balance
3. A special `BacktestRestClient` replaces the real Kraken API client
4. Your Lua script is loaded and `on_init()` is called
5. For each candle in the data:
   - The current candle becomes the "live" market data
   - `on_tick()` is called on your script
   - When your script calls `trader.get_ticker()`, it receives the current candle's close price
   - When your script calls `trader.get_ohlc()`, it receives all candles up to the current one
   - When your script calls `trader.market_order()`, the order fills at the current candle's close price
6. After all candles are processed, `on_stop()` is called
7. Performance metrics are computed and displayed

## Tips

- Your existing scripts work without modification in backtest mode
- The `trader.subscribe()` and `trader.unsubscribe()` calls are no-ops in backtest mode (no WebSocket)
- Market orders fill at the candle close price
- Limit orders fill if the candle's high/low crosses the limit price
- The backtest uses the `check_interval` in your script, but since each tick is one candle, you may want to set `check_interval = 1` for backtesting
- Commission is applied as a percentage of the trade's notional value (volume * price)
