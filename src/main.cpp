
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "sntp.h"
}

#include "filesystem.h"
#include "webserver.h"

static int ICACHE_FLASH_ATTR upload_fs(const text_entry& query,
                                       const text_entry& headers,
                                       const text_entry& payload)
{
#if 0
    if (!button)
        return 404;
#endif

    const auto ctype = get_header(headers, "Content-Type:");
    if (os_strncmp(ctype.text, "application/octet-stream", ctype.len))
        return 400;

    if (!write_fs(payload.text, payload.len))
        return 400;

    // TODO send 200
    return 0;
}

static const handler_entry web_handlers[] = {
    { POST_METHOD, "upload_fs", upload_fs }
};

extern "C" void ICACHE_FLASH_ATTR user_init()
{
    init_filesystem();

    configure_webserver(&web_handlers[0], sizeof(web_handlers) / sizeof(web_handlers[0]));

    static os_timer_t timer;
    os_timer_disarm(&timer);
    os_timer_setfn(&timer, [](void*) ICACHE_FLASH_ATTR {
            const auto timestamp = sntp_get_current_timestamp();
            if (timestamp) {
                const auto real_time = sntp_get_real_time(timestamp);
                os_printf("time: %s", real_time);
            }
        },
        nullptr);
    os_timer_arm(&timer, 1000, true);
}
