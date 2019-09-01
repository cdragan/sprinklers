
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
    // 'dow' is days of week.  If true, 'days' is a bitmask saying on which
    // days watering occurs.  If false, 'days' is just a number saying
    // that watering occurs every N days, where 0 means disabled,
    // 1 means every day, 2 means every other day, etc.
    bool       dow      : 1;
    char       name[21];
};

struct config_settings : public config_base {
    zone_settings zones[6];
    uint16_t      last_log_idx;
};

enum log_code {
    LOG_ZERO,           // Not a valid log entry

    LOG_CONFIG_UPDATE,  // No data
    LOG_AUTO_START,     // Data is zone index
    LOG_AUTO_END,       // Data is zone index
    LOG_MANUAL_START,   // Data is zone index
    LOG_MANUAL_END,     // Data is zone index
    LOG_BOOT,           // Data is boot_code
    LOG_MOISTURE,       // Data is moisture sensor reading (percentage)

    LOG_INVALID         // Invalid log entry (this and larger values)
};

enum boot_code {
    LOG_BOOT_POWER_ON,
    LOG_BOOT_WATCHDOG,
    LOG_BOOT_EXCEPTION,
    LOG_BOOT_SOFT_WATCHDOG,
    LOG_BOOT_SOFT_RESET,
    LOG_BOOT_DEEP_SLEEP_AWAKE,
    LOG_BOOT_EXT_RESET
};

constexpr int log_code_bits = 4;

struct log_entry {
    uint32_t timestamp;
    log_code event : log_code_bits;
    uint32_t data  : 32 - log_code_bits;
};

static_assert(sizeof(log_entry) == 8u, "Invalid size of log_entry");

constexpr size_t config_size = 0x1000u;

struct config : public config_settings {
    log_entry log[(config_size - sizeof(config_settings)) / sizeof(log_entry)];
};

static_assert(sizeof(config) <= config_size, "Incorrect config size");
static_assert(config_size - sizeof(config) < sizeof(log_entry), "Incorrect config size");

// Logs a specific event.
//
// Returns true on success and false on failure.
bool log_event(log_code event, uint32_t data = 0u);

// Returns event history.
//
// - offset - index of past event at which to start returning history, 0 is the
//            last logged event, 1 is the event before that, and so on.
// - buffer - buffer to be filled with past events.
// - size   - number of entries available in the buffer which can be filled.
//
// Returns the number of events actually written to the buffer.
unsigned get_event_history(unsigned offset, log_entry* buffer, unsigned size);
