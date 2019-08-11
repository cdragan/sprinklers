
#include "mock_access.h"
#include "../src/webserver.h"
#include "../src/filesystem.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define check_response(buffer, expected) do {                    \
    static const char val[] = (expected);                        \
    assert((buffer).size() >= sizeof(val) - 1);                  \
    assert(strncmp((buffer).data(), val, sizeof(val) - 1) == 0); \
} while (0)

#define check_string(buffer, str) do {                                      \
    static const char val[] = (str);                                        \
    assert(memmem((buffer).data(), (buffer).size(), val, sizeof(val) - 1)); \
} while (0)

static bool get_handler_called  = false;
static bool post_handler_called = false;

int main(int argc, char* argv[])
{
    if (mock::set_args(argc, argv))
        return 1;

    // GET request without any files, without any handlers
    {
        mock::clear_flash();

        assert(init_filesystem() == 1);

        configure_webserver(nullptr, 0);

        mock::buffer response;

        {
            static const char request[]  = "GET / HTTP/1.1";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 400 Bad Request\r\n");
            response.clear();
        }

        {
            static const char request[]  = "GET / HTTP/1.1\r\n";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 404 Not Found\r\n");
            response.clear();
        }

        {
            static const char request[]  = "GET /\r\n";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 400 Bad Request\r\n");
            response.clear();
        }

        mock::destroy_filesystem();
    }

    // Files and handler
    {
        mock::clear_flash();

        static const mock::file_desc files[] = {
            { "afile.css",  "a" },
            { "bfile",      "b" },
            { "index.html", "index" }
        };

        mock::load_fs_from_memory(files, sizeof(files) / sizeof(files[0]));

        assert(init_filesystem() == 0);

        static const handler_entry web_handlers[] = {
            { POST_METHOD, "somepost", [](void*             conn,
                                          const text_entry& query,
                                          const text_entry& headers,
                                          unsigned          payload_offset,
                                          const text_entry& payload) -> HTTPStatus
                {
                    post_handler_called = true;
                    assert(conn);
                    assert(query.len == 0);
                    assert(headers.len);
                    static const char expected[] = "test payload";
                    assert(payload_offset == 0);
                    assert(payload.len == sizeof(expected) - 1);
                    assert(memcmp(payload.text, expected, sizeof(expected) - 1) == 0);
                    return HTTP_OK;
                }
            },
            { GET_METHOD, "someget", [](void*             conn,
                                        const text_entry& query,
                                        const text_entry& headers,
                                        unsigned          payload_offset,
                                        const text_entry& payload) -> HTTPStatus
                {
                    get_handler_called = true;
                    assert(conn);
                    assert(query.len == 0);
                    assert(headers.len == 0);
                    assert(payload_offset == 0);
                    assert(payload.len == 0);

                    static const char data[] = "hello";
                    char buf[sizeof(data) - 1 + HTTP_HEAD_SIZE];
                    memcpy(&buf[HTTP_HEAD_SIZE], data, sizeof(data) - 1);
                    webserver_send_response(conn, buf, "some/thing", HTTP_HEAD_SIZE, sizeof(data) - 1);
                    return HTTP_RESPONSE_SENT;
                }
            },
        };

        configure_webserver(&web_handlers[0], sizeof(web_handlers) / sizeof(web_handlers[0]));

        mock::buffer response;

        {
            static const char request[] = "GET / HTTP/1.1\r\n";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 200 OK\r\n");
            check_string(response, "Content-Type: text/html\r\n");
            check_string(response, "Content-Length: 5\r\n");
            check_string(response, "\r\n\r\nindex");
            response.clear();
        }

        {
            static const char request[] = "GET /index.html HTTP/1.1\r\n";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 200 OK\r\n");
            check_string(response, "Content-Type: text/html\r\n");
            check_string(response, "Content-Length: 5\r\n");
            check_string(response, "\r\n\r\nindex");
            response.clear();
        }

        {
            static const char request[] = "GET /afile.css HTTP/1.1\r\n";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 200 OK\r\n");
            check_string(response, "Content-Type: text/css\r\n");
            check_string(response, "Content-Length: 1\r\n");
            check_string(response, "\r\n\r\na");
            response.clear();
        }

        {
            static const char request[] = "GET /bfile HTTP/1.1\r\n";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 404 Not Found\r\n");
            response.clear();
        }

        {
            static const char request[] = "GET /somepost HTTP/1.1\r\n";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 404 Not Found\r\n");
            response.clear();
        }

        {
            static const char request[] = "POST /someget HTTP/1.1\r\n";
            send_http(request, sizeof(request) - 1, &response);

            check_response(response, "HTTP/1.1 404 Not Found\r\n");
            response.clear();
        }

        {
            assert( ! get_handler_called);

            static const char request[] = "GET /someget HTTP/1.1\r\n";
            send_http(request, sizeof(request) - 1, &response);

            assert(get_handler_called);

            check_response(response, "HTTP/1.1 200 OK\r\n");
            check_string(response, "Content-Type: some/thing\r\n");
            check_string(response, "Content-Length: 5\r\n");
            check_string(response, "\r\n\r\nhello");
            response.clear();
        }

        {
            assert( ! post_handler_called);

            static const char request[] = "POST /somepost HTTP/1.1\r\n"
                                          "Content-Length: 12\r\n"
                                          "\r\n"
                                          "test payload";
            send_http(request, sizeof(request) - 1, &response);
            //printf("%.*s\n", static_cast<int>(response.size()), response.data());

            assert(post_handler_called);

            check_response(response, "HTTP/1.1 200 OK\r\n");
            response.clear();
        }

        mock::destroy_filesystem();
    }

    return 0;
}
