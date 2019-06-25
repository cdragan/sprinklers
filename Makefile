PLATFORMIO = ~/.platformio/penv/bin

build:
	$(PLATFORMIO)/platformio run

upload:
	$(PLATFORMIO)/platformio run --target upload

monitor:
	$(PLATFORMIO)/pio device monitor --port /dev/cu.SLAB_USBtoUART --baud 74880

upload_and_monitor: upload
	@$(MAKE) monitor

fs.bin:
	python tools/mkfs.py www $@

.PHONY: build upload monitor upload_and_monitor
