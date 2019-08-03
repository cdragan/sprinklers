
cpp_files  = mock.cpp
cpp_files += ../src/filesystem.cpp
cpp_files += fs_unit.cpp

fs_unit: $(cpp_files)
	c++ -std=c++11 -g -O0 -D UNIT_TEST -Wall -Wextra -pedantic -Werror -fsanitize=address,undefined -I inc -o $@ $^

test: fs_unit
	./fs_unit
