
#include "configlog.h"

extern "C" {
#include "osapi.h"
#include "sntp.h"
}

config* ICACHE_FLASH_ATTR get_config()
{
    config* cfg = static_cast<config*>(load_config());
    if ( ! cfg)
        return cfg;

    if (cfg->id != ~0u)
        return cfg;

    cfg->last_watering      = 0;
    cfg->start_time         = 0;
    cfg->enabled            = false;
    cfg->last_log_idx       = 0xFFFFu;
    cfg->moisture_threshold = 0xFFFFu;

    for (uint32_t i = 0; i < num_zones; i++) {
        auto& zone = cfg->zones[i];

        zone.order    = static_cast<zone_order>(i + 1);
        zone.time_min = 0;
        zone.days     = 0;
        zone.dow      = false;

        for (size_t j = 0; j < sizeof(zone.name); j++)
            zone.name[j] = 0;
    }

    return cfg;
}

bool ICACHE_FLASH_ATTR log_event(log_code event, uint32_t data)
{
    if (event <= LOG_ZERO || event >= LOG_INVALID)
        return false;

    config* cfg = get_config();
    if ( ! cfg)
        return false;

    const uint32_t timestamp = sntp_get_current_timestamp();
    if ( ! timestamp)
        return false;

    constexpr uint16_t max_log_entries = sizeof(cfg->log) / sizeof(cfg->log[0]);

    if (cfg->id != ~0u) {

        config* next = static_cast<config*>(load_config(1));
        if ( ! next)
            return false;

        os_memcpy(cfg->log, next->log, sizeof(cfg->log));

        cfg->last_log_idx = next->last_log_idx;
    }

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
    if ( ! buffer || ! size)
        return 0u;

    const unsigned num_log_sectors = get_num_log_sectors();

    config* cfg;

    constexpr uint16_t max_log_entries = sizeof(cfg->log) / sizeof(cfg->log[0]);

    if (offset >= num_log_sectors * max_log_entries)
        return 0u;

    unsigned num = 0;

    for ( ; num < size; num++) {

        const unsigned idx = offset + num;

        const unsigned sec_idx = idx % num_log_sectors;

        cfg = static_cast<config*>(load_config(-static_cast<int>(sec_idx)));

        if (cfg->last_log_idx >= max_log_entries)
            break;

        const uint16_t log_idx_offs = idx / num_log_sectors;

        if (log_idx_offs >= max_log_entries)
            break;

        const unsigned log_idx = (cfg->last_log_idx >= log_idx_offs)
                                 ? cfg->last_log_idx - log_idx_offs
                                 : max_log_entries + cfg->last_log_idx - log_idx_offs;

        const auto entry = cfg->log[log_idx];

        if (entry.timestamp == ~0u ||
            entry.event <= LOG_ZERO ||
            entry.event >= LOG_INVALID)
            break;

        buffer[num] = cfg->log[log_idx];
    }

    return num;
}
