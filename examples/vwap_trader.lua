-- VWAP (Volume Weighted Average Price) Strategy
-- ==============================================
-- Calculates VWAP from OHLC data. Buys when price crosses below VWAP
-- (discount) and sells when price crosses above VWAP (premium).
-- Commonly used by institutional traders as a benchmark.
--
-- Parameters:
--   pair      - Trading pair (e.g., "XBTUSD")
--   interval  - OHLC candle interval in minutes (e.g., 5)
--   quantity  - Order size in base currency units
--   band_pct  - Percentage band around VWAP to avoid noise (e.g., 0.3)

-- Strategy state
local state = {
    vwap = 0,
    cumulative_volume = 0,
    cumulative_tp_volume = 0,  -- sum of (typical_price * volume)
    position = "flat",
    entry_price = 0,
    tick_count = 0,
    sample_interval = 15,
    total_trades = 0,
    last_side_of_vwap = nil,  -- "above" or "below"
}

function on_init()
    log.info("VWAP strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Interval: %d minutes", params.interval))
    log.info(string.format("  Quantity: %.6f", params.quantity))
    log.info(string.format("  Band: %.2f%%", params.band_pct))

    trader.subscribe("ticker", { params.pair })

    -- Build initial VWAP from OHLC data
    local result = trader.get_ohlc(params.pair, params.interval)
    if result.success and result.data then
        for _, candle_data in pairs(result.data) do
            if type(candle_data) == "table" then
                for _, candle in ipairs(candle_data) do
                    -- OHLC: [time, open, high, low, close, vwap, volume, count]
                    if type(candle) == "table" and #candle >= 7 then
                        local high = tonumber(candle[3])
                        local low = tonumber(candle[4])
                        local close = tonumber(candle[5])
                        local volume = tonumber(candle[7])

                        if high and low and close and volume and volume > 0 then
                            local typical_price = (high + low + close) / 3
                            state.cumulative_tp_volume = state.cumulative_tp_volume + (typical_price * volume)
                            state.cumulative_volume = state.cumulative_volume + volume
                        end
                    end
                end
            end
        end

        if state.cumulative_volume > 0 then
            state.vwap = state.cumulative_tp_volume / state.cumulative_volume
            log.info(string.format("  Initial VWAP: %.2f", state.vwap))
        end
    end

    if state.vwap == 0 then
        log.warn("  Could not calculate initial VWAP, will build from live data")
    end
end

function on_tick()
    state.tick_count = state.tick_count + 1

    if state.tick_count % state.sample_interval ~= 0 then
        return
    end

    -- Get current price and volume
    local result = trader.get_ticker(params.pair)
    if not result.success then
        return
    end

    local current_price = nil
    local current_volume = nil

    if result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                current_price = tonumber(v.c[1])
                if v.v then current_volume = tonumber(v.v[1]) end
                break
            end
        end
    end

    if not current_price then
        return
    end

    -- Update VWAP with current data (use price as typical price for live updates)
    if current_volume and current_volume > 0 then
        -- Use incremental volume updates
        local volume_delta = current_volume * 0.01  -- Approximate per-tick volume contribution
        state.cumulative_tp_volume = state.cumulative_tp_volume + (current_price * volume_delta)
        state.cumulative_volume = state.cumulative_volume + volume_delta

        if state.cumulative_volume > 0 then
            state.vwap = state.cumulative_tp_volume / state.cumulative_volume
        end
    end

    if state.vwap == 0 then
        return
    end

    -- Calculate distance from VWAP
    local distance_pct = ((current_price - state.vwap) / state.vwap) * 100
    local band = params.band_pct

    -- Determine which side of VWAP the price is on
    local current_side = nil
    if distance_pct > band then
        current_side = "above"
    elseif distance_pct < -band then
        current_side = "below"
    end

    -- Periodic logging
    if state.tick_count % (state.sample_interval * 20) == 0 then
        log.info(string.format("Price: %.2f | VWAP: %.2f | Distance: %.2f%% | Pos: %s",
                               current_price, state.vwap, distance_pct, state.position))
    end

    -- Trading logic: trade on VWAP crossovers
    if current_side == "below" and state.last_side_of_vwap == "above" then
        -- Price crossed below VWAP - buy signal (price at discount)
        if state.position ~= "long" then
            log.info(string.format("BUY: price %.2f crossed below VWAP %.2f (%.2f%%)",
                                   current_price, state.vwap, distance_pct))

            if state.position == "short" then
                trader.market_order(params.pair, "buy", params.quantity)
            end

            local order = trader.market_order(params.pair, "buy", params.quantity)
            if order.success then
                state.position = "long"
                state.entry_price = current_price
                state.total_trades = state.total_trades + 1
                log.info(string.format("Opened LONG at %.2f (trade #%d)",
                                       current_price, state.total_trades))
            end
        end

    elseif current_side == "above" and state.last_side_of_vwap == "below" then
        -- Price crossed above VWAP - sell signal (price at premium)
        if state.position ~= "short" then
            log.info(string.format("SELL: price %.2f crossed above VWAP %.2f (%.2f%%)",
                                   current_price, state.vwap, distance_pct))

            if state.position == "long" then
                trader.market_order(params.pair, "sell", params.quantity)
            end

            local order = trader.market_order(params.pair, "sell", params.quantity)
            if order.success then
                state.position = "short"
                state.entry_price = current_price
                state.total_trades = state.total_trades + 1
                log.info(string.format("Opened SHORT at %.2f (trade #%d)",
                                       current_price, state.total_trades))
            end
        end
    end

    -- Update last known side
    if current_side then
        state.last_side_of_vwap = current_side
    end
end

function on_data(channel, pair, data)
    if channel == "ticker" and pair == params.pair then
        if data and data.c and data.v then
            local price = tonumber(data.c[1])
            local volume = tonumber(data.v[1])
            if price and volume and volume > 0 then
                local vol_increment = volume * 0.001
                state.cumulative_tp_volume = state.cumulative_tp_volume + (price * vol_increment)
                state.cumulative_volume = state.cumulative_volume + vol_increment
                if state.cumulative_volume > 0 then
                    state.vwap = state.cumulative_tp_volume / state.cumulative_volume
                end
            end
        end
    end
end

function on_stop()
    log.info(string.format("VWAP strategy stopping (trades: %d, final VWAP: %.2f, position: %s)",
                           state.total_trades, state.vwap, state.position))

    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
        log.info("Flattened position on stop")
    end

    trader.unsubscribe("ticker", { params.pair })
end
