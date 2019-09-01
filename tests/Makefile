
cpp_files  = mock.cpp
cpp_files += ../src/configlog.cpp
cpp_files += ../src/filesystem.cpp
cpp_files += ../src/webserver.cpp

all_tests  = configlog_unit
all_tests += fs_unit
all_tests += webserver_unit

output_dir = ../.pio/build/tests

build: $(addprefix $(output_dir)/, $(all_tests))

$(output_dir):
	mkdir -p $(output_dir)

clean:
	rm -rf $(output_dir)

define test_build_rule
$(output_dir)/$1: $1.cpp $(cpp_files) $(wildcard inc/*.h) $(output_dir)
	c++ -std=c++11 -g -O0 -D UNIT_TEST -Wall -Wextra -pedantic -Werror -fsanitize=address,undefined -I inc -o $$@ $$(filter %.cpp, $$^)
endef

$(foreach test, $(all_tests), $(eval $(call test_build_rule,$(test))))

define run_test
run.$1: $(output_dir)/$1
	$(output_dir)/$1 $(output_dir)/$1.output
.PHONY: run.$1
test: run.$1
endef

$(foreach test, $(all_tests), $(eval $(call run_test,$(test))))

.PHONY: build clean
