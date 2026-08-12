-- Minimal Example Script
-- ======================
-- Demonstrates the basic structure of a trading script.
-- Simply logs ticker prices without placing any orders.
--
-- Use this as a template for writing your own strategies.

local tick_count = 0

function on_init()
    -- Called once when the script starts
    log.info("Minimal example script started!")
    log.info("Script name: " .. trader.name)
    log.info("Timestamp: " .. tostring(trader.timestamp()))

    -- Access parameters from config
    if params.pair then
        log.info("Configured pair: " .. params.pair)
    else
        log.info("No pair configured, using XBTUSD")
        params.pair = "XBTUSD"
    end

    -- Subscribe to real-time data (optional)
    trader.subscribe("ticker", { params.pair })
end

function on_tick()
    -- Called every tick (configurable interval, default 100ms)
    tick_count = tick_count + 1

    -- Only do work every 100 ticks to avoid rate limits
    if tick_count % 100 ~= 0 then
        return
    end

    -- Fetch ticker data
    local result = trader.get_ticker(params.pair)
    if result.success then
        log.info("Tick #" .. tick_count .. " - Got ticker data")

        -- Example: access balance
        local balance = trader.get_balance()
        if balance.success then
            log.debug("Balance query successful")
        end
    else
        log.warn("Ticker fetch failed: " .. (result.error or "unknown"))
    end
end

function on_data(channel, pair, data)
    -- Called when real-time WebSocket data arrives
    -- Only fires if you've subscribed to channels in on_init()
    log.debug(string.format("Received %s data for %s", channel, pair))
end

function on_stop()
    -- Called when the script is stopped (cleanup)
    log.info(string.format("Minimal example stopped after %d ticks", tick_count))
    trader.unsubscribe("ticker", { params.pair })
end
