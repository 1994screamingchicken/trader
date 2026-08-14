#!/usr/bin/env python3
"""
Download historical OHLC data from Kraken's public API for backtesting.

Kraken's API returns ~720 candles per request, so this tool paginates using
the 'since' parameter to collect months of data.

Usage:
    python3 tools/download_history.py --pair XBTUSD --interval 60 --months 3
    python3 tools/download_history.py --pair ETHUSD --interval 15 --months 6
    python3 tools/download_history.py --pair SOLUSD --interval 1440 --months 1
"""

import argparse
import json
import os
import sys
import time
import urllib.request
import urllib.error
from datetime import datetime, timezone


KRAKEN_OHLC_URL = "https://api.kraken.com/0/public/OHLC"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Download historical OHLC data from Kraken for backtesting."
    )
    parser.add_argument(
        "--pair",
        type=str,
        default="XBTUSD",
        help="Kraken trading pair (default: XBTUSD)",
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=60,
        help="Candle interval in minutes (default: 60). Valid: 1, 5, 15, 30, 60, 240, 1440, 10080, 21600",
    )
    parser.add_argument(
        "--months",
        type=int,
        default=3,
        help="Number of months of history to download (default: 3)",
    )
    parser.add_argument(
        "--output",
        type=str,
        default=None,
        help="Output CSV file path (default: data/{pair}_{interval}m.csv)",
    )
    return parser.parse_args()


def fetch_ohlc(pair, interval, since):
    """Fetch OHLC data from Kraken API starting from the given timestamp."""
    url = f"{KRAKEN_OHLC_URL}?pair={pair}&interval={interval}&since={since}"
    req = urllib.request.Request(url)
    req.add_header("User-Agent", "kraken-trader-downloader/1.0")

    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            data = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        print(f"HTTP error {e.code}: {e.reason}", file=sys.stderr)
        return None, None
    except urllib.error.URLError as e:
        print(f"URL error: {e.reason}", file=sys.stderr)
        return None, None
    except Exception as e:
        print(f"Request failed: {e}", file=sys.stderr)
        return None, None

    if data.get("error") and len(data["error"]) > 0:
        print(f"Kraken API error: {data['error']}", file=sys.stderr)
        return None, None

    result = data.get("result", {})
    last_timestamp = result.get("last")

    # Find the pair key (Kraken may return XXBTZUSD for XBTUSD, etc.)
    candles = None
    for key in result:
        if key != "last":
            candles = result[key]
            break

    return candles, last_timestamp


def estimate_total_candles(months, interval_minutes):
    """Estimate the total number of candles expected for the given time range."""
    total_minutes = months * 30 * 24 * 60  # approximate
    return total_minutes // interval_minutes


def main():
    args = parse_args()

    # Validate interval
    valid_intervals = [1, 5, 15, 30, 60, 240, 1440, 10080, 21600]
    if args.interval not in valid_intervals:
        print(
            f"Error: Invalid interval {args.interval}. Valid intervals: {valid_intervals}",
            file=sys.stderr,
        )
        sys.exit(1)

    # Determine output path
    if args.output is None:
        output_path = f"data/{args.pair}_{args.interval}m.csv"
    else:
        output_path = args.output

    # Calculate start timestamp
    now = int(time.time())
    seconds_per_month = 30 * 24 * 60 * 60
    since = now - (args.months * seconds_per_month)

    estimated_total = estimate_total_candles(args.months, args.interval)

    print(f"Downloading {args.pair} OHLC data (interval={args.interval}m)")
    print(f"Period: {args.months} months ({datetime.fromtimestamp(since, tz=timezone.utc).strftime('%Y-%m-%d')} to now)")
    print(f"Estimated candles: ~{estimated_total}")
    print(f"Output: {output_path}")
    print()

    # Collect all candles, keyed by timestamp for deduplication
    all_candles = {}
    request_count = 0

    while since < now:
        candles, last_timestamp = fetch_ohlc(args.pair, args.interval, since)

        if candles is None:
            print("Retrying in 5 seconds...", file=sys.stderr)
            time.sleep(5)
            continue

        if len(candles) == 0:
            break

        request_count += 1

        # Add candles to our collection (dedup by timestamp)
        new_count = 0
        for candle in candles:
            ts = int(candle[0])
            if ts not in all_candles:
                all_candles[ts] = candle
                new_count += 1

        total_collected = len(all_candles)
        if estimated_total > 0:
            pct = min(100, int(total_collected / estimated_total * 100))
        else:
            pct = 0

        print(f"Downloaded {total_collected}/{estimated_total} candles ({pct}%)")

        # If we got no new candles or last_timestamp hasn't advanced, we're done
        if new_count == 0 or last_timestamp is None:
            break

        # Use the last timestamp for the next request
        since = int(last_timestamp)

        # If since is beyond now, we're done
        if since >= now:
            break

        # Rate limiting: sleep 1-2 seconds between requests
        sleep_time = 1.5
        time.sleep(sleep_time)

    if not all_candles:
        print("Error: No data downloaded.", file=sys.stderr)
        sys.exit(1)

    # Sort by timestamp
    sorted_timestamps = sorted(all_candles.keys())
    total_candles = len(sorted_timestamps)

    print(f"\nTotal candles collected: {total_candles}")
    print(f"API requests made: {request_count}")

    # Ensure output directory exists
    output_dir = os.path.dirname(output_path)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)

    # Write CSV
    with open(output_path, "w") as f:
        f.write("timestamp,open,high,low,close,volume\n")
        for ts in sorted_timestamps:
            candle = all_candles[ts]
            # Kraken OHLC format: [timestamp, open, high, low, close, vwap, volume, count]
            timestamp = int(candle[0])
            open_price = candle[1]
            high_price = candle[2]
            low_price = candle[3]
            close_price = candle[4]
            volume = candle[6]  # index 6 is volume (index 5 is vwap)
            f.write(f"{timestamp},{open_price},{high_price},{low_price},{close_price},{volume}\n")

    first_ts = datetime.fromtimestamp(sorted_timestamps[0], tz=timezone.utc)
    last_ts = datetime.fromtimestamp(sorted_timestamps[-1], tz=timezone.utc)

    print(f"Date range: {first_ts.strftime('%Y-%m-%d %H:%M')} to {last_ts.strftime('%Y-%m-%d %H:%M')} UTC")
    print(f"Saved to: {output_path}")
    print("\nDone! You can now backtest with:")
    print(f"  ./kraken_trader --backtest --data ../{output_path} --script ../scripts/top_crypto_trader.lua --pair XBT/USD --balance 10000")


if __name__ == "__main__":
    main()
