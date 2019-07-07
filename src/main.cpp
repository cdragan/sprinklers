
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "sntp.h"
}

#include "filesystem.h"
#include "webserver.h"

// Group 1, enabled by default: 0 2 4 5
// * GPIO 0:
//      Beware, pulling it low will cause boot to fail, use only as output.
// * GPIO 2:
//      The built-in LED is also connected in addition to the
//      external pin and is driven with LO (instead of HI).
//      Also, gpio_init() initializes this to LO and lights up the
//      built-in LED.
//
// Group 2, each pin requires explicit routing through MUX: 1 3 10 12 13 14 15
// * Enable pin routing:
//      PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_GPIO1);
//      PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_GPIO3);
//      PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA3_U, FUNC_GPIO10);
//      PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
//      PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
//      PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
//      PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
// * GPIO1 is UART TX and destroys UART output
//
// Group 3, unusable GPIOs which affect system functioning: 9
//
// Group 4: 6 7 8 11 16
//  ???

/*
 *      Initial state:
 *
 *      GPIO    State
 *      =============
 *      0       HI
 *      2       lo
 *      4       lo
 *      5       lo
 *      1       HI
 *      3       HI
 *      10      HI
 *      12      HI
 *      13      HI
 *      14      HI
 *      15      HI
 */

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

// Here is the zone assignment on the NodeMCU v1.0 board
//                 +----------+
//                 | A0    D0 | GPIO16
//                 | RSV   D1 | GPIO5
//                 | RSV   D2 | GPIO4
// Zone 5 - GPIO10 | SD3   D3 | GPIO0
//           GPIO9 | SD2   D4 | GPIO2  - Green LED (status good)
//                 | SD1  3v3 |
//                 | CMD  GND |
//                 | SD0   D5 | GPIO14 - Zone 1
//                 | CLK   D6 | GPIO12 - Zone 2
//                 | GND   D7 | GPIO13 - Zone 3
//                 | 3v3   D8 | GPIO15 - Zone 6
//                 | EN    D9 | GPIO3  - Zone 4
//                 | RST  D10 | GPIO1
//                 | GND  GND |
//                 | Vin  3v3 |
//                 +---|USB|--+
//
// To supply power:
//  * Use USB or (but not simultaneously!)
//  * Put 5V on the Vin PIN and ground on the GND pin next to it.
//
// The 3 remaining 3v3 and GND pairs can be used as reference voltage to power
// stuff outside of the board.
//
// GPIO9 is unusable, when this pin is switched to GPIO on the MUX, the board
// keeps rebooting.
//
// GPIO1 is used for UART TX bit and so it is unusable, unless we wanted to lose
// the ability to use UART.
//
// GPIO16 looks like it is unusable (but I may be wrong).
//
// GPIOs 5, 4, 0, 2 have LOW state after boot and gpio_init() is called.
// The remaining GPIOs have HIGH state.
//
// GPIO2 is connected to the built-in LED (mounted close to the GPIO16 output pin).
// This built-in LED is lit when GPIO2 is in LOW state (the default after gpio_init())
// and it is not lit when GPIO2 is in HIGH state.
//
// GPIO0 must not be pulled low or the board won't boot.  It is used to indicate
// boot mode during boot, after boot it can be used for anything.
//
// GPIO15 momentarily comes up in LOW state right after boot and then goes up.
// Because of this, we use it for zone 6, which is the least likely to be used.

enum zone_status {
    ZONE_OFF, // TODO make DISABLED the default (0)
    ZONE_DISABLED,
    ZONE_ON
};

constexpr int      num_zones             = 6;
static const int   zone_gpios[num_zones] = { 14, 12, 13, 3, 10, 15 };
static zone_status zones[num_zones]      = { };

// Either turns a specific zone OFF, or turns exactly one zone ON.
// If ON is requested, any remaining zone that is currently on will be turned OFF.
// GPIOs 14, 12, 13, 3, 10, 15 are normally in HIGH state after boot.
// The signal to control the relay must be LOW to turn the relay's electromagnet ON.
// This is a lucky coincidence, so GPIO state HIGH means that the relay is OFF
// and GPIO in LOW state means the relay is ON.
// Note: GPIO 15 momentarily goes to LOW on boot, turning the relay on for a brief
// moment until we init GPIOs (something like 400ms-ish).
static void ICACHE_FLASH_ATTR zone_on_off(int zone, int on)
{
    for (int i = 0; i < num_zones; i++) {
        if (zones[i] == ZONE_ON && (i != zone || ! on)) {
            os_printf("zone %d off\n", i);
            gpio(zone_gpios[i]).write(hi);
            zones[i] = ZONE_OFF;
        }
    }

    if (on && zones[zone] == ZONE_OFF) {
        os_printf("zone %d on\n", zone);
        gpio(zone_gpios[zone]).write(lo);
        zones[zone] = ZONE_ON;
    }
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

    if (zones[zone] == ZONE_DISABLED) {
        os_printf("Error: zone %d is disabled\n", zone);
        return HTTP_BAD_REQUEST;
    }

    zone_on_off(zone, state_char - '0');

    return HTTP_OK;
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
    os_timer_arm(&timer, 5000, true);
}

static const handler_entry web_handlers[] = {
    { GET_METHOD,  "sysinfo",   sysinfo   },
    { POST_METHOD, "upload_fs", upload_fs },
    { PUT_METHOD,  "manual",    manual    }
};

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

    init_filesystem();

    // TODO read config

    configure_webserver(&web_handlers[0], sizeof(web_handlers) / sizeof(web_handlers[0]));

    system_init_done_cb(init_done);
}
