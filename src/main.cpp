
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
    os_printf("==== QUERY ====\n");
    os_printf("%s", query.text);
    os_printf("\n==== HEADERS ====\n");
    os_printf("%s", headers.text);

    for (int i = 0; i < payload.len; i++) {
        if ((i % 16) == 0) {
            if (i)
                os_printf("\n");
            os_printf("%04x:", i);
        }
        os_printf(" %02x", (uint8_t)payload.text[i]);
    }
    os_printf("\n");
    return 500;
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

    static os_timer_t t2;
    os_timer_disarm(&t2);
    os_timer_setfn(&t2, [](void*) ICACHE_FLASH_ATTR {

           static uint32_t skip = 0;
           if (skip < 400) {
               ++skip;
               return;
           }

           static uint32_t addr = 0;
           static unsigned last_all_efs = 0;
           static uint32_t total_time_us = 0;
           static unsigned num_meas = 0;
           static uint32_t max_time_us = 0;

           if (addr >= 0x400000)
               return;

           uint32_t data[1024];
           const auto t0 = system_get_time();
           const auto result = spi_flash_read(addr, data, 4096);
           const auto delta = system_get_time() - t0;
           if (result == SPI_FLASH_RESULT_OK) {
               bool all_efs = true;
               for (uint32_t i = 0; i < sizeof(data) / sizeof(data[0]); i++) {
                   if (data[i] != ~0U) {
                       all_efs = false;
                       break;
                   }
               }
               if (all_efs) {
                   if (!last_all_efs)
                       os_printf("%08X: ALL Fs\n", addr);
                   else if (last_all_efs == 1)
                       os_printf("          :::\n");
                   ++last_all_efs;
               }
               else {
                   last_all_efs = 0;
                   os_printf("%08X:", addr);
                   for (unsigned i = 0; i < 8; i++)
                       os_printf(" %08X", data[i]);
                   os_printf(" ...\n");
               }
           }
           else
               os_printf("%08X: %s\n", addr,
                         result == SPI_FLASH_RESULT_ERR ? "error" :
                         result == SPI_FLASH_RESULT_TIMEOUT ? "timeout" :
                         "failed: status unknown");

           addr += 0x1000;

           total_time_us += delta;
           if (delta > max_time_us)
               max_time_us = delta;
           ++num_meas;

           if (addr == 0x400000) {
               os_printf("END OF FLASH\n");
               os_printf("Read time: avg=%u us, max=%u us\n",
                         total_time_us / num_meas, max_time_us);
           }
       },
       nullptr);
    os_timer_arm(&t2, 10, true);
}
