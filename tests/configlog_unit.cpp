
#include "mock_access.h"
#include "../src/configlog.h"
#include <assert.h>
#include <string.h>

constexpr unsigned sec_per_day     = 60u * 60u * 24u;
constexpr unsigned num_log_entries = sizeof(config::log) / sizeof(log_entry);

int main(int argc, char* argv[])
{
    if (mock::set_args(argc, argv))
        return 1;

    {
        mock::clear_flash();

        assert(!log_event(LOG_CONFIG_UPDATE));

        assert(load_config() != nullptr);

        assert(!log_event(LOG_CONFIG_UPDATE));

        log_entry e[10] = { };

        assert(get_event_history(1, e, sizeof(e) / sizeof(e[0])) == 0u);

        mock::set_timestamp(123u);

        assert(log_event(LOG_CONFIG_UPDATE, 42u));

        mock::reboot();
        assert(load_config() != nullptr);

        assert(get_event_history(1, e, sizeof(e) / sizeof(e[0])) == 0u);

        assert(get_event_history(0, e, sizeof(e) / sizeof(e[0])) == 1u);

        assert(e[0].timestamp == 123u);
        assert(e[0].event     == LOG_CONFIG_UPDATE);
        assert(e[0].data      == 42u);

        assert(e[1].timestamp == 0u);

        mock::set_timestamp(sec_per_day);

        assert(!log_event(LOG_ZERO));

        assert(!log_event(LOG_INVALID));

        assert(log_event(LOG_CONFIG_UPDATE, 43u));

        memset(e, 0, sizeof(e));

        assert(get_event_history(0, e, sizeof(e) / sizeof(e[0])) == 2u);

        assert(e[0].timestamp == sec_per_day);
        assert(e[0].event     == LOG_CONFIG_UPDATE);
        assert(e[0].data      == 43u);

        assert(e[1].timestamp == 123u);
        assert(e[1].event     == LOG_CONFIG_UPDATE);
        assert(e[1].data      == 42u);

        assert(e[2].timestamp == 0u);

        mock::destroy_filesystem();
    }

    {
        mock::clear_flash();

        assert(init_filesystem() == 1);

        const auto num_log_sectors = get_num_log_sectors();
        const auto max_log_entries = num_log_sectors * num_log_entries;
        const auto last_entry      = max_log_entries + 3u;

        for (unsigned i = 0; i <= last_entry; i++) {

            mock::set_timestamp((i + 1u) * sec_per_day);

            assert(log_event(LOG_CONFIG_UPDATE, i));
        }

        for (unsigned i = 0; i < max_log_entries; i++) {

            log_entry e;

            assert(get_event_history(i, &e, 1) == 1u);

            const unsigned idx = last_entry - i;

            assert(e.timestamp == (idx + 1u) * sec_per_day);
            assert(e.event     == LOG_CONFIG_UPDATE);
            assert(e.data      == idx);
        }

        log_entry e;

        assert(get_event_history(max_log_entries, &e, 1) == 0u);

        assert(get_event_history(0, &e, 0) == 0u);
        assert(get_event_history(0, nullptr, 1u) == 0u);

        mock::destroy_filesystem();
    }

    return 0;
}
