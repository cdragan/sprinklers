
#ifndef WEBSERVER_H_INCLUDED
#define WEBSERVER_H_INCLUDED

#define HTTP_HEAD_SIZE 80

enum request_type {
    GET_METHOD,
    POST_METHOD,
    PUT_METHOD
};

struct text_entry {
    char* text;
    int   len;

    char* begin() { return text; }
    char* end()   { return text + len; }
    const char* begin() const { return text; }
    const char* end() const   { return text + len; }
};

enum HTTPStatus {
    HTTP_RESPONSE_SENT         = 0, // internal, indicates that handler sent a response
    HTTP_CONTINUE              = 100,
    HTTP_OK                    = 200,
    HTTP_BAD_REQUEST           = 400,
    HTTP_NOT_FOUND             = 404,
    HTTP_INTERNAL_SERVER_ERROR = 500,
    HTTP_SERVICE_UNAVAILABLE   = 503
};

typedef HTTPStatus (*request_handler)(void*             conn,
                                      const text_entry& query,
                                      const text_entry& headers,
                                      unsigned          payload_offset,
                                      const text_entry& payload);

struct handler_entry {
    request_type    method;
    const char*     uri;
    request_handler handler;
};

// Configures the webserver, to be called from user_init()
void configure_webserver(const handler_entry* user_request_handlers,
                         unsigned             num_user_handlers);

// Configures NTP, to be called from callback installed with system_init_done_cb()
void configure_ntp();

void start_wps();

text_entry get_header(const text_entry& headers, const char* header_name);

void webserver_send_response(void*       conn,
                             char*       buf,
                             const char* mime_type,
                             int         head_room,
                             int         payload_size);

#endif
