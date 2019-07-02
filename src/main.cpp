
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "sntp.h"
}

#include "filesystem.h"
#include "webserver.h"

static HTTPStatus ICACHE_FLASH_ATTR upload_fs(void*             conn,
                                              const text_entry& query,
                                              const text_entry& headers,
                                              unsigned          payload_offset,
                                              const text_entry& payload)
{
#if 0
    if (!button)
        return HTTP_NOT_FOUND;
#endif

    const auto ctype = get_header(headers, "Content-Type:");
    if (os_strncmp(ctype.text, "application/octet-stream", ctype.len)) {
        os_printf("Error: invalid content type\n");
        return HTTP_BAD_REQUEST;
    }

    if (write_fs(payload_offset, payload.text, payload.len))
        return HTTP_BAD_REQUEST;

    return HTTP_CONTINUE;
}

static int ICACHE_FLASH_ATTR safe_concat(char* buf, int buf_size, int pos, const char* in)
{
    const auto in_len  = os_strlen(in);
    const int  new_pos = pos + in_len;
    if (new_pos < buf_size)
        os_memcpy(&buf[pos], in, in_len);
    else
        os_printf("overflow %d\n", new_pos - HTTP_HEAD_SIZE);
    return new_pos;
}

static HTTPStatus ICACHE_FLASH_ATTR sysinfo(void*             conn,
                                            const text_entry& query,
                                            const text_entry& headers,
                                            unsigned          payload_offset,
                                            const text_entry& payload)
{
    char response[HTTP_HEAD_SIZE + 256];
    char tmp[32];
    int  pos = HTTP_HEAD_SIZE;

    const auto print_json = [&response, &pos](const char* in) ICACHE_FLASH_ATTR {
        pos = safe_concat(response, sizeof(response), pos, in);
    };

    print_json("{\"sdk\":\"");
    print_json(system_get_sdk_version());

    print_json("\",\"heap_free\":");
    os_sprintf(tmp, "%u", system_get_free_heap_size());
    print_json(tmp);

    print_json(",\"uptime_us\":");
    os_sprintf(tmp, "%u", system_get_time());
    print_json(tmp);

    print_json(",\"reset_reason\":");
    const rst_info* const reset_info = system_get_rst_info();
    os_sprintf(tmp, "%u", reset_info->reason);
    print_json(tmp);

    const uint32_t cur_time = sntp_get_current_timestamp();
    print_json(",\"cur_time\":");
    if (cur_time) {
        print_json("\"");
        const char* time_str = sntp_get_real_time(cur_time);
        const char* eol      = os_strchr(time_str, '\n');
        const int   len      = eol ? eol - time_str : os_strlen(time_str);
        const int   len2     = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
        os_memcpy(tmp, time_str, len2);
        tmp[len2] = 0;
        print_json(tmp);
        print_json("\"");
    }
    else
        print_json("null");

    print_json(",\"timezone\":");
    os_sprintf(tmp, "%d", sntp_get_timezone());
    print_json(tmp);

    print_json(",\"wifi_mode\":");
    const uint8_t wifi_mode = wifi_get_opmode();
    os_sprintf(tmp, "%u", wifi_mode);
    print_json(tmp);

    if (wifi_mode == STATION_MODE) {
        print_json(",\"ip\":");
        const uint8_t conn_status = wifi_station_get_connect_status();
        if (conn_status == STATION_GOT_IP) {

            ip_info ipconfig;
            if (wifi_get_ip_info(STATION_IF, &ipconfig)) {
                os_sprintf(tmp, "\"%u.%u.%u.%u\"",
                           ipconfig.ip.addr & 0xFFU,
                           (ipconfig.ip.addr >> 8) & 0xFFU,
                           (ipconfig.ip.addr >> 16) & 0xFFU,
                           ipconfig.ip.addr >> 24);
                print_json(tmp);
            }
            else
                print_json("null");
        }
        else
            print_json("null");
    }

    uint8_t m[6];
    if (wifi_get_macaddr(STATION_IF, &m[0])) {
        print_json(",\"mac\":\"");
        os_sprintf(tmp, "%02X:%02X:%02X:%02X:%02X:%02X\"",
                   m[0], m[1], m[2], m[3], m[4], m[5]);
        print_json(tmp);
    }

    print_json("}");

    const int end = pos > sizeof(response) ? sizeof(response) : pos;
    webserver_send_response(conn, response, "application/json", HTTP_HEAD_SIZE, end - HTTP_HEAD_SIZE);

    return HTTP_RESPONSE_SENT;
}

static void ICACHE_FLASH_ATTR init_done()
{
    os_printf("init done\n");

    configure_ntp();

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

static const handler_entry web_handlers[] = {
    { GET_METHOD,  "sysinfo",   sysinfo },
    { POST_METHOD, "upload_fs", upload_fs }
};

extern "C" void ICACHE_FLASH_ATTR user_init()
{
    os_printf("boot version %u\n", system_get_boot_version());
    os_printf("boot mode %u\n", system_get_boot_mode());

    init_filesystem();

    // TODO read config

    configure_webserver(&web_handlers[0], sizeof(web_handlers) / sizeof(web_handlers[0]));

    system_init_done_cb(init_done);
}
