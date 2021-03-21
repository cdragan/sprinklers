build:
	pio run

upload:
	pio run --target upload

monitor:
	pio device monitor --port /dev/cu.SLAB_USBtoUART --baud 74880

upload_and_monitor: upload
	@$(MAKE) monitor

fs.bin: tools/mkfs.py $(wildcard www/*)
	python tools/mkfs.py www $@

upload_fs: fs.bin
ifdef ip
	curl --data-binary @fs.bin --header "Content-Type: application/octet-stream" http://$(ip)/upload_fs
else
	@echo "ip not specified!!! Usage: make upload_fs ip=sprinklers.local"
	@false
endif

sysinfo:
	curl http://$(ip)/sysinfo

test:
	$(MAKE) -C tests test

.PHONY: build upload monitor upload_and_monitor upload_fs sysinfo test
