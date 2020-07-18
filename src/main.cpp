
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "sntp.h"
}

#include "filesystem.h"
#include "webserver.h"
#include "configlog.h"

static config* cfg = nullptr;

enum zone_status {
    XZONE_OFF,
    XZONE_DISABLED,
    XZONE_ON
};

static const int   zone_gpios[num_zones] = { 14, 12, 13, 3, 10, 15 };
static zone_status zones[num_zones]      = { };

enum lohi {
    lo,
    hi
};

class gpio {
    public:
        explicit gpio(int id) : id(id) { }
        void init_output(lohi value) {
            GPIO_OUTPUT_SET(id, value ? 1 : 0);
        }
        void init_input() {
            GPIO_DIS_OUTPUT(id);
        }
        lohi read() const {
            return (GPIO_REG_READ(GPIO_IN_ADDRESS) & BIT(id)) ? hi : lo;
        }
        void write(lohi value) {
            const int reg = value ? GPIO_OUT_W1TS_ADDRESS : GPIO_OUT_W1TC_ADDRESS;
            GPIO_REG_WRITE(reg, 1 << id);
        }

    private:
        int id;
};

static void ICACHE_FLASH_ATTR set_critical_error()
{
    // TODO turn on red LED to indicate critical error
}

static void ICACHE_FLASH_ATTR clear_critical_error()
{
    // TODO turn off red LED to indicate critical error
}

// Either turns a specific zone OFF, or turns exactly one zone ON.
// See README.md for zone assignments and explanation of the behavior of
// the GPIOs.  Generally HIGH state means that a zone is OFF, which is
// the default after boot, and LOW state means that a zone is ON.
static void ICACHE_FLASH_ATTR zone_on_off(int zone, int on)
{
    for (uint32_t i = 0; i < num_zones; i++) {
        if (zones[i] == XZONE_ON && (i != zone || ! on)) {
            os_printf("zone %d off\n", i);
            gpio(zone_gpios[i]).write(hi);
            zones[i] = XZONE_OFF;
        }
    }

    if (on && zones[zone] == XZONE_OFF) {
        os_printf("zone %d on\n", zone);
        gpio(zone_gpios[zone]).write(lo);
        zones[zone] = XZONE_ON;
    }
}

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

    return HTTP_OK;
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
                           ipconfig.ip.addr & 0xFFu,
                           (ipconfig.ip.addr >> 8) & 0xFFu,
                           (ipconfig.ip.addr >> 16) & 0xFFu,
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

static HTTPStatus ICACHE_FLASH_ATTR manual(void*             conn,
                                           const text_entry& query,
                                           const text_entry& headers,
                                           unsigned          payload_offset,
                                           const text_entry& payload)
{
    // {"zone":#,"state":#}
    if (payload.len != 20) {
        os_printf("Error: payload length %d, but should be 14\n", query.len);
        return HTTP_BAD_REQUEST;
    }

    if (os_memcmp(payload.text, "{\"zone\":", 8) != 0) {
        os_printf("Error: query does not start with zone=\n");
        return HTTP_BAD_REQUEST;
    }

    if (os_memcmp(&payload.text[9], ",\"state\":", 9) != 0) {
        os_printf("Error: expected &state=\n");
        return HTTP_BAD_REQUEST;
    }

    const char zone_char = payload.text[8];
    if (zone_char < '1' || zone_char > ('0' + num_zones)) {
        os_printf("Error: bad zone %c\n", zone_char);
        return HTTP_BAD_REQUEST;
    }

    const char state_char = payload.text[18];
    if (state_char != '0' && state_char != '1') {
        os_printf("Error: bad state %c\n", state_char);
        return HTTP_BAD_REQUEST;
    }

    const int zone = zone_char - '1';

    if (zones[zone] == XZONE_DISABLED) {
        os_printf("Error: zone %d is disabled\n", zone);
        return HTTP_BAD_REQUEST;
    }

    zone_on_off(zone, state_char - '0');

    return HTTP_OK;
}

enum how_to_run {
    RUN_AUTO,
    RUN_MANUAL
};

static void ICACHE_FLASH_ATTR select_zone(uint32_t zone_idx, how_to_run how)
{
    // TODO
}

static const handler_entry web_handlers[] = {
    { GET_METHOD,  "sysinfo",   sysinfo   },
    { POST_METHOD, "upload_fs", upload_fs },
    { PUT_METHOD,  "manual",    manual    }
};

constexpr uint32_t update_interval_s = 10;
constexpr uint32_t ntp_timeout_s     = 60;

static void ICACHE_FLASH_ATTR update_schedule_from_config(uint32_t timestamp)
{
    // TODO
}

static void ICACHE_FLASH_ATTR update_zones(void*)
{
    static uint32_t bad_updates = 0;

    const auto timestamp = sntp_get_current_timestamp();

    if ( ! timestamp) {
        ++bad_updates;
        os_printf("Error: no time from SNTP!\n");
        if (bad_updates * update_interval_s >= ntp_timeout_s)
            set_critical_error();
        return;
    }

    clear_critical_error();
    bad_updates = 0;

    static bool initial_update = false;

    if (!initial_update) {
        initial_update = true;

        update_schedule_from_config(timestamp);
    }

    uint32_t zone_to_run = 0;

    for ( ; zone_to_run < num_zones; zone_to_run++) {

        const auto& zone = cfg->zones[zone_to_run];
        //TODO
    }

    select_zone(zone_to_run, RUN_AUTO);
}

extern "C" void ICACHE_FLASH_ATTR user_init()
{
    os_printf("boot version %u\n", system_get_boot_version());
    os_printf("boot mode %u\n", system_get_boot_mode());

    gpio_init();

    // Enable output GPIOs for zones
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
    gpio(14).init_output(hi);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
    gpio(12).init_output(hi);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
    gpio(13).init_output(hi);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
    gpio(15).init_output(hi);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_GPIO3);
    gpio(3).init_output(hi);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA3_U, FUNC_GPIO10);
    gpio(10).init_output(hi);

    cfg = get_config();

    if (!cfg) {
        os_printf("Error: failed to read configuration!\n");
        set_critical_error();
        return;
    }

    init_filesystem();

    configure_webserver(&web_handlers[0], sizeof(web_handlers) / sizeof(web_handlers[0]));

    system_init_done_cb([]() ICACHE_FLASH_ATTR {

        os_printf("init done\n");

        configure_ntp();

        static os_timer_t timer;
        os_timer_disarm(&timer);
        os_timer_setfn(&timer, update_zones, nullptr);
        os_timer_arm(&timer, update_interval_s * 1000, true);
    });
}
