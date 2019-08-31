
#include "sprinkler_config.h"

extern "C" {
#include "osapi.h"
#include "sntp.h"
}

static ICACHE_FLASH_ATTR config* get_config()
{
    config* cfg = static_cast<config*>(load_config());
    if ( ! cfg)
        return cfg;

    if (cfg->id != ~0u)
        return cfg;

    // TODO init default zone config etc.

    return cfg;
}

bool ICACHE_FLASH_ATTR log_event(log_code event, uint32_t data)
{
    config* cfg = get_config();
    if ( ! cfg)
        return false;

    config* next = static_cast<config*>(load_config(1));
    if ( ! next)
        return false;

    constexpr uint16_t max_log_entries = sizeof(cfg->log) / sizeof(cfg->log[0]);

    os_memcpy(cfg->log, next->log, sizeof(cfg->log));

    const uint32_t timestamp = sntp_get_current_timestamp();

    uint16_t idx = cfg->last_log_idx + 1u;
    if (idx >= max_log_entries)
        idx = 0;
    cfg->last_log_idx = idx;

    log_entry& e = cfg->log[idx];
    e.timestamp = timestamp;
    e.event     = event;
    e.data      = data;

    return save_config(cfg) == 0;
}

unsigned ICACHE_FLASH_ATTR get_event_history(unsigned offset, log_entry* buffer, unsigned size)
{
    const unsigned num_log_sectors = get_num_log_sectors();

    config* cfg;

    constexpr uint16_t max_log_entries = sizeof(cfg->log) / sizeof(cfg->log[0]);

    if (offset >= num_log_sectors * max_log_entries)
        return 0u;

    unsigned num = 0;

    for ( ; num < size; num++) {

        const unsigned idx = offset + num;

        cfg = static_cast<config*>(load_config(-static_cast<int>(idx)));

        if (cfg->last_log_idx >= max_log_entries)
            break;

        const uint16_t log_idx_offs = idx / num_log_sectors;

        if (log_idx_offs >= max_log_entries)
            break;

        uint16_t log_idx = cfg->last_log_idx - log_idx_offs;

        if (log_idx >= max_log_entries)
            log_idx = max_log_entries - 1u;

        const auto entry = cfg->log[log_idx];

        if (entry.timestamp == ~0u ||
            entry.event == LOG_ZERO ||
            entry.event >= LOG_INVALID)
            break;

        buffer[num] = cfg->log[log_idx];
    }

    return num;
}
