
#ifndef WEBSERVER_H_INCLUDED
#define WEBSERVER_H_INCLUDED

enum request_type {
    GET_METHOD,
    POST_METHOD
};

struct text_entry {
    char* text;
    int   len;
};

typedef int (*request_handler)(const text_entry& query,
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

#endif
