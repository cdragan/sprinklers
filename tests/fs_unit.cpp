
#include "mock_access.h"
#include "../src/filesystem.h"
#include <assert.h>
#include <string.h>

int main()
{
    // Init empty filesystem
    {
        mock::clear_flash();

        //mock::load_fs_from_file("../fs.bin");

        assert(init_filesystem() == 1);

        mock::destroy_filesystem();
    }

    // Init filesystem with one empty file
    {
        mock::clear_flash();

        static const mock::file_desc files[] = {
            { "test", "" }
        };

        mock::load_fs_from_memory(files, sizeof(files) / sizeof(files[0]));

        assert(init_filesystem() == 0);

        assert(find_file("") == nullptr);

        assert(find_file("nosuch") == nullptr);

        assert(find_file("tes") == nullptr);

        assert(find_file("tesp") == nullptr);

        assert(find_file("test ") == nullptr);

        const auto f = find_file("test");
        assert(f != nullptr);

        assert(load_file(f) == nullptr);

        mock::destroy_filesystem();
    }

    // Init filesystem with two non-empty files
    {
        mock::clear_flash();

        static const mock::file_desc files[] = {
            { "f1", "1" },
            { "f2", "2" }
        };

        mock::load_fs_from_memory(files, sizeof(files) / sizeof(files[0]));

        assert(init_filesystem() == 0);

        const auto f1 = find_file("f1");
        assert(f1 != nullptr);

        char* buf = load_file(f1);
        assert(buf != nullptr);
        assert(*buf == '1');
        free(buf);

        assert(load_file(f1, 1) == nullptr);
        assert(load_file(f1, 2) == nullptr);
        assert(load_file(f1, 3) == nullptr);

        buf = load_file(f1, 4);
        assert(buf != nullptr);
        assert(buf[4] == '1');
        free(buf);

        const auto f2 = find_file("f2");
        assert(f2 != nullptr);

        buf = load_file(f2, 12);
        assert(buf != nullptr);
        assert(buf[12] == '2');
        free(buf);

        assert(find_file("f") == nullptr);

        assert(find_file("f11") == nullptr);

        assert(find_file("") == nullptr);

        mock::destroy_filesystem();
    }

    // Write file system, one sector
    {
        mock::clear_flash();

        static const mock::file_desc files[] = {
            { "empty", "" },
            { "one", "$" },
            { "two", "42" }
        };

        assert(init_filesystem() == 1);

        {
            mock::fsmaker maker;
            maker.construct(files, sizeof(files) / sizeof(files[0]));

            assert(write_fs(0u, static_cast<const char*>(maker.get_buffer()), maker.get_size()) == 0);
        }

        const auto empty = find_file("empty");
        assert(empty != nullptr);

        assert(load_file(empty) == nullptr);

        const auto one = find_file("one");
        assert(one != nullptr);

        auto buf = load_file(one, 128u);
        assert(buf[128u] == '$');
        free(buf);

        const auto two = find_file("two");
        assert(two != nullptr);

        buf = load_file(two);
        assert(buf[0u] == '4');
        assert(buf[1u] == '2');
        free(buf);

        mock::destroy_filesystem();
    }

    // Detect corruption on write of directory
    {
        mock::clear_flash();

        static const mock::file_desc files[] = {
            { "something", "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789" }
        };

        mock::fsmaker maker;
        maker.construct(files, sizeof(files) / sizeof(files[0]));

        assert(write_fs(0u, static_cast<const char*>(maker.get_buffer()), maker.get_size()) == 0);

        assert(init_filesystem() == 0);

        filesystem* fs = static_cast<filesystem*>(maker.get_buffer());

        // Bad checksum
        fs->entries[0].size = 0;
        assert(write_fs(0u, static_cast<const char*>(maker.get_buffer()), maker.get_size()) == 1);

        fs->entries[0].size = 100;
        assert(write_fs(0u, static_cast<const char*>(maker.get_buffer()), maker.get_size()) == 0);

        // Bad number of files
        fs->num_files = 146;
        assert(write_fs(0u, static_cast<const char*>(maker.get_buffer()), maker.get_size()) == 1);

        mock::destroy_filesystem();
    }

    // Write other sectors
    {
        mock::clear_flash();

        static const char stuff[] = "stuff##";

        // Filesystem not initialized, cannot write
        assert(write_fs(0x1000u, stuff, sizeof(stuff)) == 1);

        // Init some dummy filesystem
        static const mock::file_desc files[] = {
            { "x", "x" }
        };
        mock::fsmaker maker;
        maker.construct(files, sizeof(files) / sizeof(files[0]));
        assert(write_fs(0u, static_cast<const char*>(maker.get_buffer()), maker.get_size()) == 0);

        // Write to next sector OK
        assert(write_fs(0x1000u, stuff, sizeof(stuff)) == 0);

        // Write to last sector OK
        assert(write_fs(128u * 1024u - 0x1000u, stuff, sizeof(stuff)) == 0);

        // Cannot write beyond the end of the available area
        assert(write_fs(128u * 1024u, stuff, sizeof(stuff)) == 1);

        // Bad offsets
        assert(write_fs(0x1001u, stuff, sizeof(stuff)) == 1);
        assert(write_fs(0x1010u, stuff, sizeof(stuff)) == 1);
        assert(write_fs(0x1100u, stuff, sizeof(stuff)) == 1);
        assert(write_fs(0x1F00u, stuff, sizeof(stuff)) == 1);

        mock::destroy_filesystem();
    }

    // Detect corruption in flash
    {
        mock::clear_flash();

        static const mock::file_desc files[] = {
            { "something", "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789" }
        };

        mock::load_fs_from_memory(files, sizeof(files) / sizeof(files[0]));

        assert(init_filesystem() == 0);

        mock::destroy_filesystem();

        filesystem* const fs = nullptr;

        const auto get_offset = [](void* ptr) -> uint32_t {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr));
        };
#define OFFSET(what) (get_offset(&fs->what))

        mock::clear_flash();

        mock::load_fs_from_memory(files, sizeof(files) / sizeof(files[0]));

        // Corrupt magic number
        {
            const auto offset = 1 + OFFSET(magic);

            const auto orig_val = mock::modify_filesystem(offset, 0);
            mock::modify_filesystem(offset, orig_val ^ 0x10u);

            assert(init_filesystem() == 1);

            mock::modify_filesystem(offset, orig_val);
        }

        // Corrupt number of files
        {
            const auto offset = 1 + OFFSET(num_files);

            const auto orig_val = mock::modify_filesystem(offset, 0);
            mock::modify_filesystem(offset, orig_val ^ 0x10u);

            assert(init_filesystem() == 1);

            mock::modify_filesystem(offset, orig_val);
        }

        // Corrupt checksum
        {
            const auto offset = 1 + OFFSET(checksum);

            const auto orig_val = mock::modify_filesystem(offset, 0);
            mock::modify_filesystem(offset, orig_val ^ 0x10u);

            assert(init_filesystem() == 1);

            mock::modify_filesystem(offset, orig_val);
        }

        // Corrupt file size
        {
            const auto offset = 1 + OFFSET(entries[0].size);

            const auto orig_val = mock::modify_filesystem(offset, 0);
            mock::modify_filesystem(offset, orig_val ^ 0x10u);

            assert(init_filesystem() == 1);

            mock::modify_filesystem(offset, orig_val);
        }

        // Corrupt file contents
        {
            const auto file_size = strlen(files[0].contents);
            const auto offset = sizeof(filesystem) + file_size / 2u;

            const auto orig_val = mock::modify_filesystem(offset, 0);
            mock::modify_filesystem(offset, orig_val ^ 0x10u);

            assert(init_filesystem() == 0);

            const auto two = find_file("something");
            assert(two != nullptr);

            auto buf = load_file(two);
            assert(buf == nullptr);

            mock::modify_filesystem(offset, orig_val);

            buf = load_file(two);
            assert(buf != nullptr);

            assert(memcmp(buf, files[0].contents, file_size) == 0);
            free(buf);
        }

        mock::destroy_filesystem();
    }

    struct config : public config_base
    {
        uint8_t  stuff[1000u - sizeof(config_base) - sizeof(uint32_t)];
        uint32_t tail_id;
    };

    // Test log saving and loading
    {
        mock::clear_flash();

        config* cfg = static_cast<config*>(load_config());
        assert(cfg != nullptr);

        assert(cfg->id == ~0u);
        assert(cfg->tail_id == ~0u);

        // Fail - time not set
        assert(save_config(cfg) == 1);

        // Set initial time
        mock::set_timestamp(1);

        // Initial save
        cfg->tail_id = 0u;
        assert(save_config(cfg) == 0);
        assert(cfg->id == 0u);

        // Reboot and reload
        mock::reboot();
        cfg = static_cast<config*>(load_config());
        assert(cfg->id == 0u);
        assert(cfg->tail_id == 0u);

        // 4MB flash, 4KB per sector, 1MB for firmware, 128KB for filesystem, 5 sectors for SDK
        constexpr uint32_t usable_log_sectors = 0x400u - 0x20u - 0x100u - 5u;

        constexpr uint32_t seconds_per_day = 60u * 60u * 24u;

        uint32_t used_sectors = 1u;

        const auto advance = [&]() {
            ++cfg->tail_id;
            assert(save_config(cfg) == 0);
            ++used_sectors;
        };

        const auto advance_with_time = [&]() {
            mock::set_timestamp(seconds_per_day * used_sectors);
            advance();
        };

        const auto reboot_and_verify = [&]() {
            memset(cfg, 0, sizeof(config));

            mock::reboot();

            cfg = static_cast<config*>(load_config());
            assert(cfg->tail_id + 1u == used_sectors);
            assert(cfg->id == cfg->tail_id);
        };

        // Write half of the available log area
        while (used_sectors < usable_log_sectors / 2u)
            advance_with_time();

        assert(cfg->first_timestamp == 1);
        assert(cfg->tail_id + 1u == used_sectors);
        assert(cfg->id == cfg->tail_id);

        // Reboot and make sure we have the same stuff as last time
        reboot_and_verify();

        // Write two sectors and verify to make sure binary search works
        for (int i = 0; i < 2; ++i) {
            advance_with_time();
            reboot_and_verify();
        }

        // Max 1 write per sector until now
        assert(mock::get_flash_lifetime() == 1u);

        // Write until we use all available sectors
        while (used_sectors < usable_log_sectors)
            advance_with_time();

        // Reboot and make sure we have correct stuff
        reboot_and_verify();

        // Still max 1 write per sector until now
        assert(mock::get_flash_lifetime() == 1u);

        // Next write goes back to first sector
        advance_with_time();
        reboot_and_verify();

        // Now one sector has been written 2 times
        assert(mock::get_flash_lifetime() == 2u);

        // Write a few sectors to make sure binary search works
        for (int i = 0; i < 5; ++i) {
            advance_with_time();
            reboot_and_verify();
        }

        // Clear flash
        mock::destroy_filesystem();
        mock::clear_flash();

        // Save some files to ensure filesystem is not affected by log
        const uint32_t big_file_size = 128u * 1024u - sizeof(filesystem);
        char* const big_file_contents = static_cast<char*>(malloc(big_file_size + 1));
        memset(big_file_contents, 0xCA, big_file_size);
        big_file_contents[big_file_size] = 0;
        const mock::file_desc files[] = {
            { "big", big_file_contents }
        };
        mock::load_fs_from_memory(files, sizeof(files) / sizeof(files[0]));

        // Init for the first time
        cfg = static_cast<config*>(load_config());
        constexpr uint32_t first_timestamp = 123456u;
        mock::set_timestamp(first_timestamp);
        cfg->tail_id = 0u;
        assert(save_config(cfg) == 0);
        used_sectors = 1u;

        // Write some entries
        mock::set_timestamp(first_timestamp + 1u);
        while (used_sectors < 402u)
            advance();

        // Cannot write anymore
        assert(save_config(cfg) == 1);

        // Can write some more after some time
        mock::set_timestamp(first_timestamp + seconds_per_day / 2u);
        while (used_sectors < 602u)
            advance();

        // Cannot write again
        assert(save_config(cfg) == 1);

        // Reboot
        reboot_and_verify();

        // Still cannot write after reboot
        mock::set_timestamp(first_timestamp + seconds_per_day / 2u);
        assert(save_config(cfg) == 1);

        // Some more time passed, can write again
        mock::set_timestamp(first_timestamp + seconds_per_day);
        while (used_sectors < 802u)
            advance();

        // Cannot write again
        assert(save_config(cfg) == 1);

        // Reboot
        reboot_and_verify();

        // Timestamp not set, cannot write
        assert(save_config(cfg) == 1);

        // Ensure that filesystem is intact, not affected by config/log
        assert(init_filesystem() == 0);
        const auto big = find_file("big");
        assert(big != nullptr);
        const auto contents = load_file(big);
        assert(contents != nullptr);
        assert(memcmp(contents, big_file_contents, big_file_size) == 0);
        free(contents);
        free(big_file_contents);

        mock::destroy_filesystem();
    }

    return 0;
}
