
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

        assert(load_file(f, 0) == nullptr);

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

        char* buf = load_file(f1, 0);
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

        assert(load_file(empty, 0) == nullptr);

        const auto one = find_file("one");
        assert(one != nullptr);

        auto buf = load_file(one, 128u);
        assert(buf[128u] == '$');
        free(buf);

        const auto two = find_file("two");
        assert(two != nullptr);

        buf = load_file(two, 0u);
        assert(buf[0u] == '4');
        assert(buf[1u] == '2');
        free(buf);

        mock::destroy_filesystem();
    }

    return 0;
}
