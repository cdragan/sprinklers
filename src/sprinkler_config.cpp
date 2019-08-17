
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

bool ICACHE_FLASH_ATTR log_event(log_code event)
{
    config* cfg = get_config();
    if ( ! cfg)
        return false;

    config* next = static_cast<config*>(load_config(1));
    if ( ! next)
        return false;

    os_memcpy(cfg->log, next->log, sizeof(cfg->log));

    const uint32_t timestamp = sntp_get_current_timestamp();

    constexpr uint16_t max_log_entries = sizeof(cfg->log) / sizeof(cfg->log[0]);
    uint16_t idx = cfg->last_log_idx + 1u;
    if (idx >= max_log_entries)
        idx = 0;
    cfg->last_log_idx = idx;

    log_entry& e = cfg->log[idx];
    e.timestamp = timestamp;
    e.event     = event;

    return save_config(cfg) == 0;
}
