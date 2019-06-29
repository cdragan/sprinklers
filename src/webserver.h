
#ifndef WEBSERVER_H_INCLUDED
#define WEBSERVER_H_INCLUDED

#define HTTP_HEAD_SIZE 72

enum request_type {
    GET_METHOD,
    POST_METHOD
};

struct text_entry {
    char* text;
    int   len;

    char* begin() { return text; }
    char* end()   { return text + len; }
    const char* begin() const { return text; }
    const char* end() const   { return text + len; }
};

typedef int (*request_handler)(void*             conn,
                               const text_entry& query,
                               const text_entry& headers,
                               const text_entry& payload);

struct handler_entry {
    request_type    method;
    const char*     uri;
    request_handler handler;
};

void configure_webserver(const handler_entry* user_request_handlers,
                         unsigned             num_user_handlers);

void start_wps();

text_entry get_header(const text_entry& headers, const char* header_name);

void webserver_send_ok(void* conn);

void webserver_send_response(void*       conn,
                             char*       buf,
                             const char* mime_type,
                             int         head_room,
                             int         payload_size);

#endif
