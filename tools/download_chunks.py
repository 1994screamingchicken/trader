#!/usr/bin/env python3
"""
Download all available 5-minute candle data from Kraken for a given pair
and split it into 2-hour chunks (24 candles each).

Each chunk is saved as a separate CSV file in data/chunks/{pair}/ directory.
File naming: chunk_001_2025-08-12_14-00.csv, chunk_002_2025-08-12_16-00.csv, etc.

Usage:
    python3 tools/download_chunks.py --pair XBTUSD
    python3 tools/download_chunks.py --pair ETHUSD
    python3 tools/download_chunks.py --pair SOLUSD
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
INTERVAL = 5  # 5-minute candles
CHUNK_SIZE = 24  # 24 candles per chunk (2 hours of 5-minute data)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Download all available 5-minute candle data from Kraken and split into 2-hour chunks."
    )
    parser.add_argument(
        "--pair",
        type=str,
        default="XBTUSD",
        help="Kraken trading pair (default: XBTUSD)",
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default=None,
        help="Output directory (default: data/chunks/{pair}/)",
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


def download_all_candles(pair):
    """Download all available 5-minute candle data from Kraken."""
    print(f"Downloading all available 5-minute candle data for {pair}...")
    print(f"(Kraken typically has ~2 days of 5-minute data available)")
    print()

    # Start from ~3 days ago to ensure we get all available data
    now = int(time.time())
    since = now - (3 * 24 * 60 * 60)

    all_candles = {}
    request_count = 0

    while since < now:
        candles, last_timestamp = fetch_ohlc(pair, INTERVAL, since)

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
        print(f"  Request #{request_count}: got {len(candles)} candles, {new_count} new (total: {total_collected})")

        # If we got no new candles or last_timestamp hasn't advanced, we're done
        if new_count == 0 or last_timestamp is None:
            break

        # Use the last timestamp for the next request
        since = int(last_timestamp)

        # If since is beyond now, we're done
        if since >= now:
            break

        # Rate limiting: sleep between requests
        time.sleep(1.5)

    return all_candles, request_count


def split_into_chunks(all_candles):
    """Split candles into 2-hour chunks (24 candles each)."""
    sorted_timestamps = sorted(all_candles.keys())
    chunks = []

    for i in range(0, len(sorted_timestamps), CHUNK_SIZE):
        chunk_timestamps = sorted_timestamps[i:i + CHUNK_SIZE]
        # Only include complete chunks (exactly 24 candles)
        if len(chunk_timestamps) == CHUNK_SIZE:
            chunk_candles = [all_candles[ts] for ts in chunk_timestamps]
            chunks.append(chunk_candles)

    return chunks


def write_chunk(chunk_candles, chunk_number, output_dir):
    """Write a single chunk to a CSV file. Returns the filename."""
    # Get the timestamp of the first candle for the filename
    first_ts = int(chunk_candles[0][0])
    dt = datetime.fromtimestamp(first_ts, tz=timezone.utc)
    date_str = dt.strftime("%Y-%m-%d_%H-%M")

    filename = f"chunk_{chunk_number:03d}_{date_str}.csv"
    filepath = os.path.join(output_dir, filename)

    with open(filepath, "w") as f:
        f.write("timestamp,open,high,low,close,volume\n")
        for candle in chunk_candles:
            # Kraken OHLC format: [timestamp, open, high, low, close, vwap, volume, count]
            timestamp = int(candle[0])
            open_price = candle[1]
            high_price = candle[2]
            low_price = candle[3]
            close_price = candle[4]
            volume = candle[6]  # index 6 is volume (index 5 is vwap)
            f.write(f"{timestamp},{open_price},{high_price},{low_price},{close_price},{volume}\n")

    return filename


def main():
    args = parse_args()

    # Determine output directory
    if args.output_dir is None:
        output_dir = f"data/chunks/{args.pair}/"
    else:
        output_dir = args.output_dir

    # Download all available 5-minute candle data
    all_candles, request_count = download_all_candles(args.pair)

    if not all_candles:
        print("Error: No data downloaded.", file=sys.stderr)
        sys.exit(1)

    total_candles = len(all_candles)
    sorted_timestamps = sorted(all_candles.keys())
    first_dt = datetime.fromtimestamp(sorted_timestamps[0], tz=timezone.utc)
    last_dt = datetime.fromtimestamp(sorted_timestamps[-1], tz=timezone.utc)

    print(f"\nTotal candles collected: {total_candles}")
    print(f"API requests made: {request_count}")
    print(f"Date range: {first_dt.strftime('%Y-%m-%d %H:%M')} to {last_dt.strftime('%Y-%m-%d %H:%M')} UTC")

    # Split into 2-hour chunks
    chunks = split_into_chunks(all_candles)
    print(f"\nComplete 2-hour chunks (24 candles each): {len(chunks)}")

    incomplete_candles = total_candles - (len(chunks) * CHUNK_SIZE)
    if incomplete_candles > 0:
        print(f"Discarded {incomplete_candles} candles from incomplete final chunk")

    if len(chunks) == 0:
        print("Error: Not enough data for even one complete 2-hour chunk (need 24 candles).", file=sys.stderr)
        sys.exit(1)

    # Ensure output directory exists
    os.makedirs(output_dir, exist_ok=True)

    # Write each chunk
    print(f"\nWriting chunks to: {output_dir}")
    for i, chunk_candles in enumerate(chunks):
        filename = write_chunk(chunk_candles, i + 1, output_dir)
        chunk_start = datetime.fromtimestamp(int(chunk_candles[0][0]), tz=timezone.utc)
        chunk_end = datetime.fromtimestamp(int(chunk_candles[-1][0]), tz=timezone.utc)
        print(f"  {filename} ({chunk_start.strftime('%H:%M')} - {chunk_end.strftime('%H:%M')} UTC)")

    print(f"\nDone! {len(chunks)} chunk files saved to {output_dir}")
    print("\nBacktest a single chunk:")
    print(f"  ./kraken_trader --backtest --data ../{output_dir}chunk_001_*.csv --script ../scripts/top_crypto_trader.lua --pair XBT/USD --balance 10000")
    print("\nBacktest all chunks:")
    print(f"  bash tools/backtest_all_chunks.sh {output_dir} scripts/top_crypto_trader.lua XBT/USD 10000")


if __name__ == "__main__":
    main()
