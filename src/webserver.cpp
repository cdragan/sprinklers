
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "espconn.h"
#include "mem.h"
#include "sntp.h"
}

#include "filesystem.h"
#include "webserver.h"

static void ICACHE_FLASH_ATTR configure_mdns()
{
    static os_timer_t timer;

    os_timer_disarm(&timer);

    os_timer_setfn(&timer, [](void*) ICACHE_FLASH_ATTR {

        ip_info ipconfig;

        wifi_get_ip_info(STATION_IF, &ipconfig);

        static uint32_t last_ip = 0;

        if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr && ipconfig.ip.addr != last_ip) {

            static mdns_info mdns = { };

            // TODO get mDNS name from config

            mdns.host_name   = const_cast<char*>(MDNS_NAME);
            mdns.ipAddr      = ipconfig.ip.addr;
            mdns.server_name = const_cast<char*>(MDNS_NAME);
            mdns.server_port = 80;
            mdns.txt_data[0] = const_cast<char*>("version = 1.0");

            espconn_mdns_init(&mdns);

            last_ip = ipconfig.ip.addr;

            os_printf("configured mdns %s.local on %u.%u.%u.%u\n",
                      MDNS_NAME,
                      last_ip & 0xFFu,
                      (last_ip >> 8) & 0xFFu,
                      (last_ip >> 16) & 0xFFu,
                      last_ip >> 24);
        }
    }, nullptr);

    os_timer_arm(&timer, 1000, true);
}

void ICACHE_FLASH_ATTR start_wps()
{
    wifi_wps_disable();
    wifi_wps_enable(WPS_TYPE_PBC);
    wifi_set_wps_cb([](int status) ICACHE_FLASH_ATTR {
        switch (status) {

            case WPS_CB_ST_SUCCESS:
                wifi_wps_disable();
                wifi_station_connect();
                break;

            case WPS_CB_ST_FAILED:
                // fall through
            case WPS_CB_ST_TIMEOUT:
                wifi_wps_start();
                break;
        }
    });
    wifi_wps_start();
}

static void ICACHE_FLASH_ATTR configure_wifi()
{
    if (wifi_get_opmode() != STATION_MODE) {

        wifi_set_opmode(STATION_MODE);

#ifdef SSID
        static const char ssid[32]     = SSID;
        static const char password[64] = PASSWORD;

        struct station_config sta_conf = { };

        os_memcpy(sta_conf.ssid,     ssid,     sizeof(ssid));
        os_memcpy(sta_conf.password, password, sizeof(password));
        wifi_station_set_config(&sta_conf);

        wifi_station_connect();
#endif
    }
}

static void ICACHE_FLASH_ATTR configure_ntp()
{
    // TODO Get servers and timezone from configuration
    sntp_setservername(0, const_cast<char*>("time.euro.apple.com"));
    sntp_setservername(1, const_cast<char*>("time.google.com"));
    sntp_setservername(2, const_cast<char*>("pool.ntp.org"));

    sntp_set_timezone(1);

    sntp_init();
}

static void ICACHE_FLASH_ATTR print_conn_info(espconn* conn, const char* what)
{
    os_printf("%u.%u.%u.%u:%d %s\n",
              conn->proto.tcp->remote_ip[0],
              conn->proto.tcp->remote_ip[1],
              conn->proto.tcp->remote_ip[2],
              conn->proto.tcp->remote_ip[3],
              conn->proto.tcp->remote_port,
              what);
}

static void ICACHE_FLASH_ATTR print_conn_info(espconn* conn, const char* what, int number)
{
    os_printf("%u.%u.%u.%u:%d %s %d\n",
              conn->proto.tcp->remote_ip[0],
              conn->proto.tcp->remote_ip[1],
              conn->proto.tcp->remote_ip[2],
              conn->proto.tcp->remote_ip[3],
              conn->proto.tcp->remote_port,
              what,
              number);
}

void ICACHE_FLASH_ATTR webserver_send_response(void*       arg,
                                               char*       buf,
                                               const char* mime_type,
                                               int         head_room,
                                               int         payload_size)
{
    os_sprintf(buf, "HTTP/1.1 200 OK\nContent-Type: %s\nContent-Length: %d\n\n",
               mime_type, payload_size);

    const int head_size = os_strlen(buf);
    char*     out       = buf + head_room - head_size;

    os_memmove(out, buf, head_size);

    espconn* const conn = static_cast<espconn*>(arg);

    print_conn_info(conn, "reply 200");

    espconn_send(conn, reinterpret_cast<uint8_t*>(out), head_size + payload_size);
}

static void ICACHE_FLASH_ATTR webserver_send_error(espconn*   conn,
                                                   HTTPStatus code)
{
    const char* code_str = "500 Internal Server Error";

    switch (code) {

        case HTTP_OK:
            code_str = "200 OK";
            break;

        case HTTP_CONTINUE:
            code_str = "100 Continue";
            break;

        case HTTP_BAD_REQUEST:
            code_str = "400 Bad Request";
            break;

        case HTTP_NOT_FOUND:
            code_str = "404 Not Found";
            break;

        case HTTP_SERVICE_UNAVAILABLE:
            code_str = "503 Service Unavailable";
            break;

        default:
            break;
    }

    static const char head_str[] = "<html><body><h1>";
    static const char tail_str[] = "</h1></body></html>";

    char buf[128];
    os_sprintf(buf, "HTTP/1.1 %s\nContent-Type: text/html\nContent-Length: %d\n\n%s%s%s",
               code_str,
               static_cast<int>(sizeof(head_str) - 1 + os_strlen(code_str) + sizeof(tail_str) - 1),
               head_str,
               code_str,
               tail_str);

    print_conn_info(conn, "reply", static_cast<int>(code));

    espconn_send(conn, reinterpret_cast<uint8_t*>(buf), os_strlen(buf));
}

static const char* ICACHE_FLASH_ATTR get_mime_type(const char* uri, int len)
{
    struct mime_entry {
        char        ext[5];
        signed char len;
        const char* mime_type;
    };

    static const mime_entry mime_types[] = {
        { "html", 4, "text/html"       },
        { "css",  3, "text/css"        },
        { "js",   2, "text/javascript" }
    };

    for (const auto& e : mime_types) {
        if (len <= e.len)
            continue;

        if (uri[len - e.len - 1] != '.')
            continue;

        if (os_memcmp(&uri[len - e.len], e.ext, e.len) == 0)
            return e.mime_type;
    }

    return nullptr;
}

text_entry ICACHE_FLASH_ATTR get_header(const text_entry& headers,
                                        const char*       header_name)
{
    auto begin = os_strstr(headers.text, header_name);

    if ( ! begin)
        return text_entry{nullptr, 0};

    begin += os_strlen(header_name);

    for ( ; *begin == ' '; ++begin);

    auto end = begin;

    for (;; ++end) {
        const char c = *end;

        if (c == '\r' || c == 0)
            break;
    }

    return text_entry{begin, end - begin};
}

struct saved_conn_t {
    uint8_t         remote_ip[4];
    int             remote_port;
    unsigned        content_length;
    unsigned        num_received;
    request_handler handler;
    text_entry      query;
    text_entry      headers;
    text_entry      payload;
    char            data[1];
};

static constexpr unsigned payload_buf_size = SPI_FLASH_SEC_SIZE;

static saved_conn_t* saved_connections = nullptr;

static HTTPStatus ICACHE_FLASH_ATTR save_connection(espconn*          conn,
                                                    unsigned          content_length,
                                                    const text_entry& query,
                                                    const text_entry& headers,
                                                    request_handler   handler)
{
    if (saved_connections) {
        os_printf("Error: another connection is pending\n");
        return HTTP_SERVICE_UNAVAILABLE;
    }

    const auto alloc_size = sizeof(saved_conn_t) - 1 +
                            query.len + 1 +
                            headers.len + 1 +
                            payload_buf_size;

    if (alloc_size > 1024 + payload_buf_size) {
        os_printf("Error: query or headers too large\n");
        return HTTP_BAD_REQUEST;
    }

    const auto saved_conn = static_cast<saved_conn_t*>(os_malloc(alloc_size));

    if (!saved_conn) {
        os_printf("Error: out of memory\n");
        return HTTP_SERVICE_UNAVAILABLE;
    }

    saved_conn->remote_ip[0]   = conn->proto.tcp->remote_ip[0];
    saved_conn->remote_ip[1]   = conn->proto.tcp->remote_ip[1];
    saved_conn->remote_ip[2]   = conn->proto.tcp->remote_ip[2];
    saved_conn->remote_ip[3]   = conn->proto.tcp->remote_ip[3];
    saved_conn->remote_port    = conn->proto.tcp->remote_port;
    saved_conn->content_length = content_length;
    saved_conn->num_received   = 0;
    saved_conn->handler        = handler;

    const int headers_pos = query.len + 1;
    if (query.len)
        os_memcpy(&saved_conn->data[0], query.text, query.len + 1);
    os_memcpy(&saved_conn->data[headers_pos], headers.text, headers.len + 1);

    saved_conn->query.text = &saved_conn->data[0];
    saved_conn->query.len  = query.len;

    saved_conn->headers.text = &saved_conn->data[headers_pos];
    saved_conn->headers.len  = headers.len;

    saved_conn->payload.text = &saved_conn->data[headers_pos + headers.len + 1];
    saved_conn->payload.len  = 0;

    saved_connections = saved_conn;

    return HTTP_CONTINUE;
}

static saved_conn_t* find_saved_connection(espconn* conn)
{
    if (saved_connections &&
        saved_connections->remote_ip[0] == conn->proto.tcp->remote_ip[0] &&
        saved_connections->remote_ip[1] == conn->proto.tcp->remote_ip[1] &&
        saved_connections->remote_ip[2] == conn->proto.tcp->remote_ip[2] &&
        saved_connections->remote_ip[3] == conn->proto.tcp->remote_ip[3] &&
        saved_connections->remote_port == conn->proto.tcp->remote_port)

        return saved_connections;

    return nullptr;
}

static void ICACHE_FLASH_ATTR free_connection(espconn *conn)
{
    const auto saved_conn = find_saved_connection(conn);

    if (saved_conn) {
        os_free(saved_connections);
        saved_connections = nullptr;
    }
}

static const handler_entry* request_handlers     = nullptr;
static const handler_entry* request_handlers_end = nullptr;

static HTTPStatus ICACHE_FLASH_ATTR handle_request(espconn*          conn,
                                                   request_type      method,
                                                   const text_entry& uri,
                                                   const text_entry& query,
                                                   const text_entry& headers_and_payload)
{
    for (auto h = request_handlers; h != request_handlers_end; h++) {

        if (h->method == method && os_strcmp(uri.text, h->uri) == 0) {

            text_entry headers = headers_and_payload;
            text_entry payload = { };

            for (int i = 1; i < headers.len; i++) {

                const char c = headers.text[i];

                // Two consecutive CRLF sequences signify the end of headers
                if (c == '\r' && headers.text[i - 1] == '\n') {

                    // skip LF which should be right after CR
                    const int payload_offs = (i + 1 < headers.len) ? 2 : 1;

                    payload.text = &headers.text[i + payload_offs];
                    payload.len  = headers.len - i - payload_offs;

                    headers.len     = i;
                    headers.text[i] = 0;
                    break;
                }
            }

            // Check Content-Length in a POST request
            if (method == POST_METHOD) {

                const auto len_hdr = get_header(headers, "Content-Length:");
                if (!len_hdr.len || len_hdr.len > 5) {
                    os_printf("Error: invalid or unsupported Content-Length header\n");
                    return HTTP_BAD_REQUEST;
                }

                unsigned clen = 0;
                for (const auto c : len_hdr) {
                    if (c < '0' || c > '9') {
                        os_printf("Error: invalid Content-Length header\n");
                        return HTTP_BAD_REQUEST;
                    }
                    clen = clen * 10 + (c - '0');
                }

                // Handle Expect: 100-continue
                if (payload.len == 0 && clen > 0) {
                    const auto expect_hdr = get_header(headers, "Expect:");
                    if (os_strncmp(expect_hdr.text, "100-continue", expect_hdr.len) == 0)
                        return save_connection(conn, clen, query, headers, h->handler);
                }

                if (clen != payload.len) {
                    os_printf("Error: incorrect payload length, header says %u but it is %d\n",
                              clen, payload.len);
                    return HTTP_BAD_REQUEST;
                }
            }

            return h->handler(conn, query, headers, 0, payload);
        }
    }

    return HTTP_NOT_FOUND;
}

static void ICACHE_FLASH_ATTR recv_more_data(espconn*      conn,
                                             saved_conn_t& saved_conn,
                                             char*         pusrdata,
                                             uint16_t      length)
{
    auto& payload = saved_conn.payload;

    const auto total_size = saved_conn.num_received + payload.len + length;

    if (total_size > saved_conn.content_length) {
        os_printf("Error: received too much data\n");
        webserver_send_error(conn, HTTP_BAD_REQUEST);
        return;
    }

    const bool last_bit = total_size == saved_conn.content_length;

    if (payload.len) {

        const auto room_left = payload_buf_size - payload.len;
        const auto part_size = length > room_left ? room_left : length;
        os_memcpy(&payload.text[payload.len], pusrdata, part_size);
        payload.len += part_size;

        if (payload.len == payload_buf_size || last_bit) {

            const auto err = saved_conn.handler(conn,
                                                saved_conn.query,
                                                saved_conn.headers,
                                                saved_conn.num_received,
                                                payload);

            if (err) {
                webserver_send_error(conn, err);
                free_connection(conn);
                return;
            }

            saved_conn.num_received += payload.len;
            payload.len = 0;
        }

        pusrdata += part_size;
        length   -= part_size;
    }

    if (length && (length > payload_buf_size || last_bit)) {

        const auto part_size = last_bit ? length : (length - length % payload_buf_size);

        text_entry tmp_payload{ pusrdata, static_cast<int>(part_size) };

        const auto err = saved_conn.handler(conn,
                                            saved_conn.query,
                                            saved_conn.headers,
                                            saved_conn.num_received,
                                            tmp_payload);

        if (err) {
            webserver_send_error(conn, err);
            free_connection(conn);
            return;
        }

        saved_conn.num_received += part_size;

        pusrdata += part_size;
        length   -= part_size;
    }

    if (length) {
        os_memcpy(payload.text, pusrdata, length);
        payload.len = length;
    }

    webserver_send_error(conn, last_bit ? HTTP_OK : HTTP_CONTINUE);

    if (last_bit)
        free_connection(conn);
}

static void ICACHE_FLASH_ATTR webserver_recv(void* arg, char* pusrdata, unsigned short length)
{
    espconn* const conn = static_cast<espconn*>(arg);

    print_conn_info(conn, "receive", static_cast<int>(length));

    //========================================================================
    // Handle more incoming data from an existing connection
    //========================================================================

    const auto saved_conn = find_saved_connection(conn);
    if (saved_conn) {
        recv_more_data(conn, *saved_conn, pusrdata, length);
        return;
    }

    //========================================================================
    // Extract method, URI, HTTP version and headers
    //========================================================================

    enum state {
        method,
        uri,
        version,
        headers,
        query,

        num_states
    };

    text_entry e[num_states] = {};
    e[method].text = pusrdata;

    int s = method;

    for (unsigned i = 0; i < length; i++) {

        const char c = pusrdata[i];

        if (c == ' ' && s < headers) {

            const unsigned end = i;
            while (i < length && pusrdata[i] == ' ')
                ++i;

            pusrdata[end] = 0;

            e[s].len = pusrdata + end - e[s].text;
            ++s;
            e[s].text = pusrdata + i;
        }
        else if (c == '\n') {

            if (s == version) {
                pusrdata[i - 1] = 0; // overwrite CR with 0
                e[s].len = pusrdata + i - 1 - e[s].text;
            }

            e[headers].text = pusrdata + i + 1;
            e[headers].len  = length - i - 1;
            break;
        }
    }

    os_printf("%u.%u.%u.%u:%d %s %s\n",
              conn->proto.tcp->remote_ip[0],
              conn->proto.tcp->remote_ip[1],
              conn->proto.tcp->remote_ip[2],
              conn->proto.tcp->remote_ip[3],
              conn->proto.tcp->remote_port,
              e[method].text,
              e[uri].text);

    //========================================================================
    // Separate arguments from URI
    //========================================================================

    for (int i = 0; i < e[uri].len; i++) {
        if (e[uri].text[i] == '?') {
            e[query].text  = &e[uri].text[i + 1];
            e[query].len   = length - i - 1;
            e[uri].text[i] = 0;
            e[uri].len     = i;
            break;
        }
    }

    //========================================================================
    // Fix up the URI
    //========================================================================

    // For root URI, use index.html
    if (e[uri].len == 1 && e[uri].text[0] == '/') {

        static const char index_html[] = "index.html";
        e[uri].text = const_cast<char*>(&index_html[0]);
        e[uri].len  = sizeof(index_html) - 1;
    }
    else {
        // Skip leading slash
        if (e[uri].text[0] == '/') {
            ++e[uri].text;
            --e[uri].len;
        }

        // To lowercase
        for (int i = 0; i < e[uri].len; i++) {
            const char c = e[uri].text[i];
            if (c >= 'A' && c <= 'Z')
                e[uri].text[i] = c + 0x20;
        }
    }

    //========================================================================
    // Process GET request
    //========================================================================

    if (e[method].len == 3 && os_memcmp(e[method].text, "GET", 3) == 0 && e[uri].len) {

        const auto err = handle_request(conn, GET_METHOD, e[uri], e[query], e[headers]);

        if (err == HTTP_NOT_FOUND)  {

            // Serve a static file
            // -------------------

            constexpr int head_room = HTTP_HEAD_SIZE;
            auto fentry = find_file(e[uri].text);

            if (!fentry)
                os_printf("Error: file '%s' not found\n", e[uri].text);

            const char* mime_type = nullptr;
            if (fentry) {
                mime_type = get_mime_type(e[uri].text, e[uri].len);

                if (!mime_type)
                    os_printf("Error: cannot detect MIME type for '%s'\n",
                              e[uri].text);
            }

            char* file = nullptr;
            if (mime_type)
                file = load_file(fentry, head_room);

            if (file) {
                webserver_send_response(conn, file, mime_type, head_room, fentry->size);

                os_free(file);
            }
            else
                webserver_send_error(conn, HTTP_NOT_FOUND);
        }
        else if (err)
            webserver_send_error(conn, err);
    }

    //========================================================================
    // Process POST request
    //========================================================================

    else if (e[method].len == 4 && os_memcmp(e[method].text, "POST", 4) == 0 && e[uri].len) {

        const auto err = handle_request(conn, POST_METHOD, e[uri], e[query], e[headers]);

        if (err)
            webserver_send_error(conn, err);
    }

    //========================================================================
    // Invalid or unsupported request
    //========================================================================

    else
        webserver_send_error(conn, HTTP_BAD_REQUEST);
}

static void ICACHE_FLASH_ATTR webserver_reconnect(void* arg, sint8 err)
{
    espconn* const conn = static_cast<espconn*>(arg);

    print_conn_info(conn, "reconnect", err);
}

static void ICACHE_FLASH_ATTR webserver_disconnect(void* arg)
{
    espconn* const conn = static_cast<espconn*>(arg);

    print_conn_info(conn, "disconnect");

    free_connection(conn);
}

static void ICACHE_FLASH_ATTR webserver_listen(void* arg)
{
    espconn* const conn = static_cast<espconn*>(arg);

    espconn_regist_recvcb(conn, webserver_recv);
    espconn_regist_reconcb(conn, webserver_reconnect);
    espconn_regist_disconcb(conn, webserver_disconnect);
}

void ICACHE_FLASH_ATTR configure_webserver(const handler_entry* user_request_handlers,
                                           unsigned             num_user_handlers)
{
    request_handlers     = user_request_handlers;
    request_handlers_end = user_request_handlers + num_user_handlers;

    configure_wifi();

    configure_mdns();

    configure_ntp();

    static espconn conn;
    static esp_tcp tcp;

    conn.type      = ESPCONN_TCP;
    conn.state     = ESPCONN_NONE;
    conn.proto.tcp = &tcp;
    tcp.local_port = 80;
    espconn_regist_connectcb(&conn, webserver_listen);

    espconn_accept(&conn);
}
