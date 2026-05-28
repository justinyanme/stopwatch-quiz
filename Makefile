.PHONY: help build test install pair flash monitor clean

help:
	@echo "Targets:"
	@echo "  build           Build bridge (swift build -c release)"
	@echo "  test            Run bridge tests + firmware native tests"
	@echo "  install         Install bridge as launchd agent"
	@echo "  pair            Run bridge in foreground (verbose) to see the watch connect"
	@echo "  flash           Flash firmware to a connected M5Stack StopWatch"
	@echo "  monitor         Open serial monitor on the watch"
	@echo "  clean           Remove build artifacts"

build:
	cd bridge && swift build -c release

test:
	cd bridge && swift test
	cd firmware && pio test -e native

install: build
	./bridge/.build/release/stopwatch-bridge install

pair: build
	./bridge/.build/release/stopwatch-bridge pair

flash:
	python3 firmware/tools/zap.py || true
	cd firmware && pio run -t upload

monitor:
	cd firmware && pio device monitor -b 115200

clean:
	cd bridge && swift package clean
	cd firmware && pio run -t clean
