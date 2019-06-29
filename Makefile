PLATFORMIO = ~/.platformio/penv/bin

build:
	$(PLATFORMIO)/platformio run

upload:
	$(PLATFORMIO)/platformio run --target upload

monitor:
	$(PLATFORMIO)/pio device monitor --port /dev/cu.SLAB_USBtoUART --baud 74880

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

.PHONY: build upload monitor upload_and_monitor upload_fs
