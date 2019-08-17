
#pragma once

#include "filesystem.h"

enum zone_order {
    ZONE_DISABLED,
    ZONE_1,
    ZONE_2,
    ZONE_3,
    ZONE_4,
    ZONE_5,
    ZONE_6

    // A zone which is not configured has no name
};

enum days_of_watering {
    DAY_MONDAY    = 1,
    DAY_TUESDAY   = 2,
    DAY_WEDNESDAY = 4,
    DAY_THURSDAY  = 8,
    DAY_FRIDAY    = 16,
    DAY_SATURDAY  = 32,
    DAY_SUNDAY    = 64
};

struct zone_settings {
    // 'order' is used to sort the zones, i.e. says in which order zones
    // will be watered.
    zone_order order    : 3;
    // 'time_min' says how long a zone will be watered, in minutes.
    uint8_t    time_min : 6;
    uint8_t    days     : 7;
    // 'dow' is days of week.  If true, days is a bitmask saying on which
    // days watering occurs.  If false, days is just a number saying
    // that watering occurs every N days, where 0 means disabled,
    // 1 means every day, 2 means every other day, etc.
    bool       dow      : 1;
    char       name[21];
};

struct config_settings : public config_base {
    zone_settings zones[6];
    uint16_t      last_log_idx;
};

enum log_code : uint8_t {
    LOG_INVALID,

    LOG_CONFIG_UPDATE,

    LOG_ZONE1_AUTO_START,
    LOG_ZONE2_AUTO_START,
    LOG_ZONE3_AUTO_START,
    LOG_ZONE4_AUTO_START,
    LOG_ZONE5_AUTO_START,
    LOG_ZONE6_AUTO_START,

    LOG_ZONE1_AUTO_END,
    LOG_ZONE2_AUTO_END,
    LOG_ZONE3_AUTO_END,
    LOG_ZONE4_AUTO_END,
    LOG_ZONE5_AUTO_END,
    LOG_ZONE6_AUTO_END,

    LOG_ZONE1_MANUAL_START,
    LOG_ZONE2_MANUAL_START,
    LOG_ZONE3_MANUAL_START,
    LOG_ZONE4_MANUAL_START,
    LOG_ZONE5_MANUAL_START,
    LOG_ZONE6_MANUAL_START,

    LOG_ZONE1_MANUAL_END,
    LOG_ZONE2_MANUAL_END,
    LOG_ZONE3_MANUAL_END,
    LOG_ZONE4_MANUAL_END,
    LOG_ZONE5_MANUAL_END,
    LOG_ZONE6_MANUAL_END,

    LOG_BOOT_POWER_ON,
    LOG_BOOT_WATCHDOG,
    LOG_BOOT_EXCEPTION,
    LOG_BOOT_SOFT_WATCHDOG,
    LOG_BOOT_SOFT_RESET,
    LOG_BOOT_DEEP_SLEEP_AWAKE,
    LOG_BOOT_EXT_RESET
};

struct log_entry {
    uint32_t timestamp;
    log_code event;
};

constexpr size_t config_size = 0x1000;

struct config : public config_settings {
    log_entry log[(config_size - sizeof(config_settings)) / sizeof(log_entry)];
};

static_assert(sizeof(config) <= config_size, "Incorrect config size");
static_assert(config_size - sizeof(config) < sizeof(log_entry), "Incorrect config size");

// Logs a specific event.
//
// Returns true on success and false on failure.
bool log_event(log_code event);
