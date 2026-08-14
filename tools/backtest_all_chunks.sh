#!/usr/bin/env bash
#
# Run the backtester on every chunk file in a directory and print a summary.
#
# Usage:
#   bash tools/backtest_all_chunks.sh <chunks_dir> <script_path> <pair> [balance] [commission]
#
# Examples:
#   bash tools/backtest_all_chunks.sh data/chunks/XBTUSD/ scripts/top_crypto_trader.lua XBT/USD 10000
#   bash tools/backtest_all_chunks.sh data/chunks/ETHUSD/ scripts/top_crypto_trader.lua ETH/USD 5000 0.26
#

set -euo pipefail

# Parse arguments
CHUNKS_DIR="${1:?Usage: $0 <chunks_dir> <script_path> <pair> [balance] [commission]}"
SCRIPT_PATH="${2:?Usage: $0 <chunks_dir> <script_path> <pair> [balance] [commission]}"
PAIR="${3:?Usage: $0 <chunks_dir> <script_path> <pair> [balance] [commission]}"
BALANCE="${4:-10000}"
COMMISSION="${5:-0.26}"

# Find the backtester binary
BACKTESTER="./build/kraken_trader"
if [ ! -x "$BACKTESTER" ]; then
    # Try relative to script location
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    BACKTESTER="$PROJECT_ROOT/build/kraken_trader"
    if [ ! -x "$BACKTESTER" ]; then
        echo "Error: Cannot find kraken_trader binary." >&2
        echo "Expected at: ./build/kraken_trader or $BACKTESTER" >&2
        exit 1
    fi
fi

# Verify chunks directory exists
if [ ! -d "$CHUNKS_DIR" ]; then
    echo "Error: Chunks directory not found: $CHUNKS_DIR" >&2
    exit 1
fi

# Verify script exists
if [ ! -f "$SCRIPT_PATH" ]; then
    echo "Error: Script not found: $SCRIPT_PATH" >&2
    exit 1
fi

# Collect chunk files (sorted)
CHUNK_FILES=($(find "$CHUNKS_DIR" -name "chunk_*.csv" | sort))

if [ ${#CHUNK_FILES[@]} -eq 0 ]; then
    echo "Error: No chunk files found in $CHUNKS_DIR" >&2
    exit 1
fi

echo "=============================================="
echo "  Backtest All Chunks"
echo "=============================================="
echo "Chunks directory: $CHUNKS_DIR"
echo "Script:           $SCRIPT_PATH"
echo "Pair:             $PAIR"
echo "Balance:          $BALANCE"
echo "Commission:       $COMMISSION%"
echo "Chunks found:     ${#CHUNK_FILES[@]}"
echo "=============================================="
echo ""

# Arrays to store results
declare -a RESULTS_FILES=()
declare -a RESULTS_PNL=()
TOTAL_PNL=0
WIN_COUNT=0
LOSS_COUNT=0
EVEN_COUNT=0

# Run backtest on each chunk
for CHUNK_FILE in "${CHUNK_FILES[@]}"; do
    CHUNK_NAME=$(basename "$CHUNK_FILE")

    # Run the backtester and capture output
    OUTPUT=$("$BACKTESTER" --backtest \
        --data "$CHUNK_FILE" \
        --script "$SCRIPT_PATH" \
        --pair "$PAIR" \
        --balance "$BALANCE" \
        --commission "$COMMISSION" 2>&1) || true

    # Extract P&L from output
    PNL=$(echo "$OUTPUT" | grep -i "Total P&L:" | head -1 | grep -oP '[-+]?[0-9]*\.?[0-9]+' | head -1) || PNL="N/A"

    RESULTS_FILES+=("$CHUNK_NAME")
    RESULTS_PNL+=("$PNL")

    if [ "$PNL" != "N/A" ]; then
        # Use awk for floating point comparison and addition
        TOTAL_PNL=$(awk "BEGIN {printf \"%.2f\", $TOTAL_PNL + $PNL}")
        IS_POSITIVE=$(awk "BEGIN {print ($PNL > 0) ? 1 : 0}")
        IS_NEGATIVE=$(awk "BEGIN {print ($PNL < 0) ? 1 : 0}")
        if [ "$IS_POSITIVE" -eq 1 ]; then
            WIN_COUNT=$((WIN_COUNT + 1))
            printf "  %-45s  P&L: +%s\n" "$CHUNK_NAME" "$PNL"
        elif [ "$IS_NEGATIVE" -eq 1 ]; then
            LOSS_COUNT=$((LOSS_COUNT + 1))
            printf "  %-45s  P&L: %s\n" "$CHUNK_NAME" "$PNL"
        else
            EVEN_COUNT=$((EVEN_COUNT + 1))
            printf "  %-45s  P&L: %s\n" "$CHUNK_NAME" "$PNL"
        fi
    else
        printf "  %-45s  P&L: N/A (parse error)\n" "$CHUNK_NAME"
    fi
done

# Print summary
echo ""
echo "=============================================="
echo "  Summary"
echo "=============================================="
echo "Total chunks tested: ${#CHUNK_FILES[@]}"
echo "Winning chunks:      $WIN_COUNT"
echo "Losing chunks:       $LOSS_COUNT"
echo "Even chunks:         $EVEN_COUNT"
echo "----------------------------------------------"
echo "Combined P&L:        $TOTAL_PNL"

if [ ${#CHUNK_FILES[@]} -gt 0 ] && [ "$TOTAL_PNL" != "0" ]; then
    AVG_PNL=$(awk "BEGIN {printf \"%.2f\", $TOTAL_PNL / ${#CHUNK_FILES[@]}}")
    echo "Average P&L/chunk:   $AVG_PNL"
fi

if [ ${#CHUNK_FILES[@]} -gt 0 ]; then
    WIN_RATE=$(awk "BEGIN {printf \"%.1f\", ($WIN_COUNT / ${#CHUNK_FILES[@]}) * 100}")
    echo "Win rate:            ${WIN_RATE}%"
fi

echo "=============================================="
