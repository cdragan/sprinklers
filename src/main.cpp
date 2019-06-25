
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "sntp.h"
}

#include "filesystem.h"
#include "webserver.h"

extern "C" void ICACHE_FLASH_ATTR user_init()
{
    init_filesystem();

    configure_webserver();

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
